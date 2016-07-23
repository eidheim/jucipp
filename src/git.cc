#include "git.h"
#include <cstring>

bool Git::initialized=false;
std::mutex Git::mutex;
std::unordered_map<std::string, std::pair<std::unique_ptr<Git::Repository>, size_t> > Git::repositories;
std::mutex Git::repositories_mutex;

std::string Git::Error::message() noexcept {
  const git_error *last_error = giterr_last();
  if(last_error==nullptr)
    return std::string();
  else
    return last_error->message;
}

Git::Repository::Diff::Diff(const boost::filesystem::path &path, git_repository *repository) : repository(repository) {
  blob=std::shared_ptr<git_blob>(nullptr, [](git_blob *blob) {
    if(blob) git_blob_free(blob);
  });
  Error error;
  std::lock_guard<std::mutex> lock(mutex);
  auto spec="HEAD:"+path.generic_string();
  error.code = git_revparse_single(reinterpret_cast<git_object**>(&blob), repository, spec.c_str());
  if(error)
    throw std::runtime_error(error.message());
  
  git_diff_init_options(&options, GIT_DIFF_OPTIONS_VERSION);
  options.context_lines=0;
}

//Based on https://github.com/atom/git-diff/blob/master/lib/git-diff-view.coffee
int Git::Repository::Diff::hunk_cb(const git_diff_delta *delta, const git_diff_hunk *hunk, void *payload) noexcept {
  auto lines=static_cast<Lines*>(payload);
  auto start=hunk->new_start-1;
  auto end=hunk->new_start+hunk->new_lines-1;
  if(hunk->old_lines==0 && hunk->new_lines>0)
    lines->added.emplace_back(start, end);
  else if(hunk->new_lines==0 && hunk->old_lines>0)
    lines->removed.emplace_back(start);
  else
    lines->modified.emplace_back(start, end);
    
  return 0;
}

Git::Repository::Diff::Lines Git::Repository::Diff::get_lines(const std::string &buffer) {
  Lines lines;
  Error error;
  std::lock_guard<std::mutex> lock(mutex);
#if LIBGIT2_SOVERSION>=23
  error.code=git_diff_blob_to_buffer(blob.get(), nullptr, buffer.c_str(), buffer.size(), nullptr, &options, nullptr, nullptr, hunk_cb, nullptr, &lines);
#else
  error.code=git_diff_blob_to_buffer(blob.get(), nullptr, buffer.c_str(), buffer.size(), nullptr, &options, nullptr, hunk_cb, nullptr, &lines);
#endif
  if(error)
    throw std::runtime_error(error.message());
  return lines;
}

int Git::Repository::Diff::line_cb(const git_diff_delta *delta, const git_diff_hunk *hunk, const git_diff_line *line, void *payload) noexcept {
  auto details=static_cast<std::pair<std::string, int> *>(payload);
  auto line_nr=details->second;
  auto start=hunk->new_start-1;
  auto end=hunk->new_start+hunk->new_lines-1;
  if(line_nr==start || (line_nr>=start && line_nr<end)) {
    if(details->first.empty())
      details->first+=std::string(hunk->header, hunk->header_len);
    details->first+=line->origin+std::string(line->content, line->content_len);
  }
  return 0;
}

std::string Git::Repository::Diff::get_details(const std::string &buffer, int line_nr) {
  std::pair<std::string, int> details;
  details.second=line_nr;
  Error error;
  std::lock_guard<std::mutex> lock(mutex);
#if LIBGIT2_SOVERSION>=23
  error.code=git_diff_blob_to_buffer(blob.get(), nullptr, buffer.c_str(), buffer.size(), nullptr, &options, nullptr, nullptr, nullptr, line_cb, &details);
#else
  error.code=git_diff_blob_to_buffer(blob.get(), nullptr, buffer.c_str(), buffer.size(), nullptr, &options, nullptr, nullptr, line_cb, &details);
#endif
  if(error)
    throw std::runtime_error(error.message());
  return details.first;
}

Git::Repository::Repository(const boost::filesystem::path &path) {
  git_repository *repository_ptr;
  {
    Error error;
    std::lock_guard<std::mutex> lock(mutex);
    auto path_str=path.generic_string();
    error.code = git_repository_open_ext(&repository_ptr, path_str.c_str(), 0, nullptr);
    if(error)
      throw std::runtime_error(error.message());
  }
  repository=std::unique_ptr<git_repository, std::function<void(git_repository *)> >(repository_ptr, [](git_repository *ptr) {
    git_repository_free(ptr);
  });
  
  work_path=get_work_path();
  
  auto git_path_str=boost::filesystem::canonical(get_path()).string();
  auto git_directory=Glib::wrap(g_file_new_for_path(git_path_str.c_str())); //TODO: report missing constructor in giomm
#if defined(__OpenBSD__) || defined(__NetBSD__) || defined(__FreeBSD__) || defined(__Darwin__) || defined(__DragonFly__)
  monitor=git_directory->monitor_directory(Gio::FileMonitorFlags::FILE_MONITOR_SEND_MOVED);
#else
#if GLIBMM_MAJOR_VERSION>2 || (GLIBMM_MAJOR_VERSION==2 && GLIBMM_MINOR_VERSION>=44)
  monitor=git_directory->monitor_directory(Gio::FileMonitorFlags::FILE_MONITOR_WATCH_MOVES);
#else
  monitor=git_directory->monitor_directory(Gio::FileMonitorFlags::FILE_MONITOR_SEND_MOVED);
#endif
#endif

  monitor_changed_connection=monitor->signal_changed().connect([this](const Glib::RefPtr<Gio::File> &file,
                                                                      const Glib::RefPtr<Gio::File>&,
                                                                      Gio::FileMonitorEvent monitor_event) {
    if(monitor_event!=Gio::FileMonitorEvent::FILE_MONITOR_EVENT_CHANGES_DONE_HINT) {
      this->clear_saved_status();
    }
  }, false);
}

Git::Repository::~Repository() {
  monitor_changed_connection.disconnect();
}

std::string Git::Repository::status_string(STATUS status) noexcept {
  switch(status) {
    case STATUS::CURRENT: return "current";
    case STATUS::NEW: return "new";
    case STATUS::MODIFIED: return "modified";
    case STATUS::DELETED: return "deleted";
    case STATUS::RENAMED: return "renamed";
    case STATUS::TYPECHANGE: return "typechange";
    case STATUS::UNREADABLE: return "unreadable";
    case STATUS::IGNORED: return "ignored";
    case STATUS::CONFLICTED: return "conflicted";
    default: return "";
  }
}

int Git::Repository::status_callback(const char *path, unsigned int status_flags, void *data) noexcept {
  auto callback=static_cast<std::function<void(const char *path, STATUS status)>*>(data);
  
  STATUS status;
  if((status_flags&(GIT_STATUS_INDEX_NEW|GIT_STATUS_WT_NEW))>0)
    status=STATUS::NEW;
  else if((status_flags&(GIT_STATUS_INDEX_MODIFIED|GIT_STATUS_WT_MODIFIED))>0)
    status=STATUS::MODIFIED;
  else if((status_flags&(GIT_STATUS_INDEX_DELETED|GIT_STATUS_WT_DELETED))>0)
    status=STATUS::DELETED;
  else if((status_flags&(GIT_STATUS_INDEX_RENAMED|GIT_STATUS_WT_RENAMED))>0)
    status=STATUS::RENAMED;
  else if((status_flags&(GIT_STATUS_INDEX_TYPECHANGE|GIT_STATUS_WT_TYPECHANGE))>0)
    status=STATUS::TYPECHANGE;
#if LIBGIT2_SOVERSION>=23
  else if((status_flags&(GIT_STATUS_WT_UNREADABLE))>0)
    status=STATUS::UNREADABLE;
#endif
  else if((status_flags&(GIT_STATUS_IGNORED))>0)
    status=STATUS::IGNORED;
#if LIBGIT2_SOVERSION>=23
  else if((status_flags&(GIT_STATUS_CONFLICTED))>0)
    status=STATUS::CONFLICTED;
#endif
  else
    status=STATUS::CURRENT;
  
  if(*callback)
    (*callback)(path, status);
  
  return 0;
}

Git::Repository::Status Git::Repository::get_status() {
  {
    std::unique_lock<std::mutex> lock(saved_status_mutex);
    if(has_saved_status)
      return saved_status;
  }
  
  Status status;
  bool first=true;
  std::unique_lock<std::mutex> status_saved_lock(saved_status_mutex, std::defer_lock);
  std::function<void(const char *path, STATUS status)> callback=[this, &status, &first, &status_saved_lock](const char *path_cstr, STATUS status_enum) {
    if(first) {
      status_saved_lock.lock();
      first=false;
    }
    boost::filesystem::path rel_path(path_cstr);
    do {
      if(status_enum==STATUS::MODIFIED)
        status.modified.emplace((work_path/rel_path).generic_string());
      if(status_enum==STATUS::NEW)
        status.added.emplace((work_path/rel_path).generic_string());
      rel_path=rel_path.parent_path();
    } while(!rel_path.empty());
  };
  Error error;
  std::lock_guard<std::mutex> lock(mutex);
  error.code = git_status_foreach(repository.get(), status_callback, &callback);
  if(error)
    throw std::runtime_error(error.message());
  saved_status=status;
  has_saved_status=true;
  if(status_saved_lock)
    status_saved_lock.unlock();
  return status;
}

void Git::Repository::clear_saved_status() {
  std::unique_lock<std::mutex> lock(saved_status_mutex);
  saved_status.added.clear();
  saved_status.modified.clear();
  has_saved_status=false;
}

boost::filesystem::path Git::Repository::get_work_path() noexcept {
  std::lock_guard<std::mutex> lock(mutex);
  return Git::path(git_repository_workdir(repository.get()));
}

boost::filesystem::path Git::Repository::get_path() noexcept {
  std::lock_guard<std::mutex> lock(mutex);
  return Git::path(git_repository_path(repository.get()));
}

boost::filesystem::path Git::Repository::get_root_path(const boost::filesystem::path &path) {
  initialize();
  git_buf root = {0, 0, 0};
  {
    Error error;
    std::lock_guard<std::mutex> lock(mutex);
    auto path_str=path.generic_string();
    error.code = git_repository_discover(&root, path_str.c_str(), 0, nullptr);
    if(error)
      throw std::runtime_error(error.message());
  }
  auto root_path=Git::path(root.ptr, root.size);
  git_buf_free(&root);
  return root_path;
}

Git::Repository::Diff Git::Repository::get_diff(const boost::filesystem::path &path) {
  return Diff(path, repository.get());
}

void Git::initialize() noexcept {
  std::lock_guard<std::mutex> lock(mutex);
  if(!initialized) {
#if LIBGIT2_SOVERSION>=22
    git_libgit2_init();
#else
    git_threads_init();
#endif
    initialized=true;
  }
}

std::shared_ptr<Git::Repository> Git::get_repository(const boost::filesystem::path &path) {
  initialize();
  std::lock_guard<std::mutex> lock(repositories_mutex);
  auto root_path=std::make_shared<std::string>(Repository::get_root_path(path).generic_string());
  auto it=repositories.find(*root_path);
  Repository *repository_ptr;
  if(it!=repositories.end()) {
    it->second.second++;
    repository_ptr=it->second.first.get();
  }
  else {
    it=repositories.emplace(*root_path, std::make_pair(std::unique_ptr<Repository>(new Repository(*root_path)), 1)).first;
    repository_ptr=it->second.first.get();
  }
  return std::shared_ptr<Repository>(repository_ptr, [root_path](Repository *) {
    std::lock_guard<std::mutex> lock(repositories_mutex);
    auto it=repositories.find(*root_path);
    it->second.second--;
    if(it->second.second==0)
      repositories.erase(it);
  });
}

boost::filesystem::path Git::path(const char *cpath, size_t cpath_length) noexcept {
  if(cpath_length==static_cast<size_t>(-1))
    cpath_length=strlen(cpath);
  if(cpath_length>0 && (cpath[cpath_length-1]=='/' || cpath[cpath_length-1]=='\\'))
    return std::string(cpath, cpath_length-1);
  else
    return std::string(cpath, cpath_length);
}
