**_This project has moved to https://gitlab.com/cppit/jucipp._**

# juCi++

###### a lightweight, platform independent C++-IDE with support for C++11, C++14 and C++17 features depending on libclang version.
<!--<img src="https://gitlab.com/cppit/jucipp/blob/master/docs/images/screenshot3.png"/>-->
## About
Current IDEs struggle with C++ support due to the complexity of
the programming language. juCI++, however, is designed especially 
towards libclang with speed, stability, and ease of use in mind. 

## Features
* Platform independent
* Fast, responsive and stable (written extensively using C++11/14 features)
* Syntax highlighting for more than 100 different file types
* C++ warnings and errors on the fly
* C++ Fix-its
* Integrated Clang-Tidy checks possible through clang plugins, for instance (recreating existing build is needed):
  ```
  CXX=clang++ CXXFLAGS="-Xclang -add-plugin -Xclang clang-tidy -Xclang -plugin-arg-clang-tidy -Xclang -checks='-*,clang-analyzer-core.*'" juci [project-path]
  ```
* Debug integration, both local and remote, through lldb
* Supports the following build systems:
  * CMake
  * Meson
* Git support through libgit2
* Fast C++ autocompletion
* Tooltips showing type information and doxygen documentation (C++)
* Rename refactoring across files (C++)
* Highlighting of similar types (C++)
* Automated documentation search (C++)
* Go to declaration, implementation, methods and usages (C++)
* OpenCL and CUDA files are supported and parsed as C++
* Other file types:
  * Language server protocol support is enabled if `[language identifier]-language-server` executable is found. This executable can be a symbolic link to one of your installed language server binaries.
See [language-server-protocol/specification.md](https://github.com/Microsoft/language-server-protocol/blob/gh-pages/specification.md) for the currently defined language identifiers.
  * otherwise, only keyword and buffer completion supported
* Find symbol through Ctags
* Spell checking depending on file context
* Run shell commands within juCi++
* Regex search and replace
* Smart paste, keys and indentation
* Multiple cursors
* Auto-indentation through [clang-format](http://clang.llvm.org/docs/ClangFormat.html) or [Prettier](https://github.com/prettier/prettier) if installed
* Source minimap
* Split view
* Multiple cursors
* Full UTF-8 support
* Wayland supported with GTK+ 3.20 or newer

See [enhancements](https://gitlab.com/cppit/jucipp/labels/enhancement) for planned features.

## Screenshots
<table border="0">
<tr>
<td><img src="https://gitlab.com/cppit/jucipp/blob/master/docs/images/screenshot1c.png" width="380"/></td>
<td><img src="https://gitlab.com/cppit/jucipp/blob/master/docs/images/screenshot2c.png" width="380"/></td>
</tr><tr>
<td><img src="https://gitlab.com/cppit/jucipp/blob/master/docs/images/screenshot3c.png" width="380"/></td>
<td><img src="https://gitlab.com/cppit/jucipp/blob/master/docs/images/screenshot4b.png" width="380"/></td>
</tr>
</table>

## Dependencies
* boost-filesystem
* boost-serialization
* gtkmm-3.0
* gtksourceviewmm-3.0
* aspell
* libclang
* lldb
* libgit2
* [libclangmm](http://gitlab.com/cppit/libclangmm/) (downloaded directly with git --recursive, no need to install)
* [tiny-process-library](http://gitlab.com/eidheim/tiny-process-library/) (downloaded directly with git --recursive, no need to install)

## Installation
See [installation guide](docs/install.md).

## Documentation
See [how to build the API doc](docs/api.md).
