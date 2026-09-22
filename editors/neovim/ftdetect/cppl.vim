" Associate *.cppl with the cppl filetype. Ordinary *.cpp files keep their
" normal C++ tooling; cppl.setup() does not claim them.
autocmd BufRead,BufNewFile *.cppl set filetype=cppl
