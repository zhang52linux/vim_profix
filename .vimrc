call plug#begin("~/.vim/plugged")

Plug 'preservim/nerdtree'
Plug 'junegunn/vim-easy-align'
Plug 'python-mode/python-mode', { 'for': 'python', 'branch': 'develop' }
Plug 'ctrlpvim/ctrlp.vim'
Plug 'easymotion/vim-easymotion'
Plug 'tpope/vim-surround'
Plug 'tpope/vim-commentary'
Plug 'tpope/vim-fugitive'
Plug 'brooth/far.vim'
Plug 'majutsushi/tagbar'
Plug 'ycm-core/YouCompleteMe'
Plug 'Raimondi/delimitMate'
Plug 'Chiel92/vim-autoformat'
Plug 'soft-aesthetic/soft-era-vim'
Plug 'yuttie/inkstained-vim'
Plug 'ashfinal/vim-colors-violet'
Plug 'Shougo/neosnippet.vim'
Plug 'Shougo/neosnippet-snippets'
Plug 'andialbrecht/sqlparse'
Plug 'jiangmiao/auto-pairs'
Plug 'morhetz/gruvbox'

call plug#end()

syntax on  
set fileencodings=ucs-bom,utf-8,utf-16,gbk,big5,gb18030,latin1  
set encoding=utf-8
set fileencoding=utf-8  
set number
set incsearch    " 增量搜索
set hls          " 高亮显示 :noh 取消高亮
" echo has("clipboard") 查看vim的编译选项中是否有clipboard
set clipboard=unnamed "有clipboard的话可以让你直接复制粘贴系统剪切板的内容
set smartindent
set tabstop=4
set shiftwidth=4
set expandtab


"调节多个窗口的大小(开启鼠标功能)
set mouse=v "设置为v模式，即表示在可视模式在可以使用鼠标进行拖拉操作


"autocmd vimenter * NERDTree
let mapleader = ','  "以后使用，+ v就能够对文件进行定位
map <C-n> :NERDTreeToggle<CR>
nnoremap <leader>v :NERDTreeFind<CR>
let NERDTreeShowHidden=1

"python-mode
set completeopt-=preview
"开启警告
let g:pymode_warnings = 0
"保存文件时自动删除无用空格
let g:pymode_trim_whitespaces = 1
let g:pymode_options = 1
"显示允许的最大长度的列
let g:pymode_options_colorcolumn = 1
"设置QuickFix窗口的最大，最小高度
let g:pymode_quickfix_minheight = 3
let g:pymode_quickfix_maxheight = 10
"使用python3
let g:pymode_python = 'python3'
"使用PEP8风格的缩进
let g:pymode_indent = 1
"取消代码折叠
let g:pymode_folding = 0
"开启python-mode定义的移动方式
let g:pymode_motion = 1
"启用python-mode内置的python文档，使用K进行查找
let g:pymode_doc = 1
let g:pymode_doc_bind = 'K'
"自动检测并启用virtualenv
let g:pymode_virtualenv = 1
"不使用python-mode运行python代码
let g:pymode_run = 0
"let g:pymode_run_bind = '<Leader>r'
"不使用python-mode设置断点
let g:pymode_breakpoint = 0
"let g:pymode_breakpoint_bind = '<leader>b'
"启用python语法检查
let g:pymode_lint = 1
"修改后保存时进行检查
let g:pymode_lint_on_write = 0
"编辑时进行检查
let g:pymode_lint_on_fly = 0
let g:pymode_lint_checkers = ['pyflakes', 'pep8']
"发现错误时不自动打开QuickFix窗口
let g:pymode_lint_cwindow = 0
"侧边栏不显示python-mode相关的标志
let g:pymode_lint_signs = 0
"let g:pymode_lint_todo_symbol = 'WW'
"let g:pymode_lint_comment_symbol = 'CC'
"let g:pymode_lint_visual_symbol = 'RR'
"let g:pymode_lint_error_symbol = 'EE'
"let g:pymode_lint_info_symbol = 'II'
"let g:pymode_lint_pyflakes_symbol = 'FF'
"启用重构
let g:pymode_rope = 1
"不在父目录下查找.ropeproject，能提升响应速度
let g:pymode_rope_lookup_project = 0
"光标下单词查阅文档
let g:pymode_rope_show_doc_bind = '<C-c>d'
"项目修改后重新生成缓存
let g:pymode_rope_regenerate_on_write = 1
"开启补全，并设置<C-Tab>为默认快捷键
let g:pymode_rope_completion = 1
let g:pymode_rope_complete_on_dot = 1
let g:pymode_rope_completion_bind = '<C-Tab>'
"<C-c>g跳转到定义处，同时新建竖直窗口打开
let g:pymode_rope_goto_definition_bind = '<C-c>g'
let g:pymode_rope_goto_definition_cmd = 'vnew'
"重命名光标下的函数，方法，变量及类名
let g:pymode_rope_rename_bind = '<C-c>rr'
"重命名光标下的模块或包
let g:pymode_rope_rename_module_bind = '<C-c>r1r'
"开启python所有的语法高亮
let g:pymode_syntax = 1
let g:pymode_syntax_all = 1
"高亮缩进错误
let g:pymode_syntax_indent_errors = g:pymode_syntax_all
"高亮空格错误
let g:pymode_syntax_space_errors = g:pymode_syntax_all

"easy-motion(模糊搜索Plugin)
nmap ss <Plug>(easymotion-s2)


"tagbar的设置
nmap <F8> :TagbarToggle<CR>
" 还要使用sduo apt install ctags才能成功使用tagbar

"powerline
set rtp+=/home/z52linux/.local/lib/python2.7/site-packages/powerline/bindings/vim/
set laststatus=2  
set showtabline=2 
set noshowmode    
set t_Co=256


" vim-commentary
" gcc为注释，gcc也可以用于注释


" vim-fugitive(可以在vim中使用git的Plugin)
" :Gwrite命令相当于git add，会把当前文件添加到git工作区
" :Gstatus命令相当于git status 
" :Gcommit命令相当于git commit
" :Gedit 命令可以打开当前文件的index版本 (index是最近一次提交的文件版本，也是下一次commit提交的地方)
" :Gblame 命令等价于git blame，以交互式垂直分割窗口的方式查看文件的每一行由谁修改
" :Gmove 命令等价于 git mv 操作，会移除一个文件并重命名缓冲区
" :Gdelete 命令相当于执行了 git rm 操作，将删除一个文件的同时删除该缓冲区

"vim-surround的使用(增加，修改，删除成对的内容)
" ds(delete a surrounding)
" cs(change a surrounding)
" ys(you add a surrounding)
" 例如: cs " ' 将"改为’



" vim的代码折叠
set foldmethod=syntax " 进入vim命令模式, za即可折叠或者关闭当前块 zM关闭所有折叠 zR打开所有折叠



" noremap是不会递归的映射 (大概是no recursion)
" 前缀代表生效范围
" inoremap就只在插入(insert)模式下生效
" vnoremap只在visual模式下生效
" nnoremap就在normal模式下(狂按esc后的模式)生效

" far.vim(批量进行替换)
" :Far foo bar **/*.py
" :Fardo


"vi/vim默认不支持c++11的语法提示，添加如下让它支持
let g:syntastic_cpp_compiler_options = ' -std=c++11 -stdlib=libc++'


:"vim-autoformat
let g:formatdef_clangformat_google = '"clang-format -style google -"'  "我比较喜欢 google 风格的代码
let g:formatters_c = ['clangformat_google']
" let g:formatdef_harttle = '"astyle --style=attach --pad-oper"'
" let g:formatters_cpp = ['harttle']
" let g:formatters_java = ['harttle']
let g:formatters_cpp = ['clangformat_google']
let g:formatters_java = ['clangformat_google']
"vim-autoformat对python的支持
let g:formatdef_autopep8 = "'autopep8 - --range '.a:firstline.' '.a:lastline"
let g:formatters_python = ['autopep8']



"mysql的auto-format
let g:formatdef_sqlformat = '"sqlformat --keywords upper -"'
let g:formatters_sql = ['sqlformat']


"vim-autoformat的映射
noremap <F3> :Autoformat<CR>


"soft-era主题设置
" Vim >=8.0 or Neovim >= 0.1.5
if (has("termguicolors"))
   set termguicolors
endif

" Neovim 0.1.3 or 0.1.4
let $NVIM_TUI_ENABLE_TRUE_COLOR=1

" vim default主题的设置
colorscheme soft-era


"inkstained配置
let g:lightline = {
      \ 'colorscheme': 'inkstained',
      \ 'component': {
      \   'readonly': '%{&readonly?"":""}',
      \ },
      \ 'separator':    { 'left': '', 'right': '' },
      \ 'subseparator': { 'left': '', 'right': '' },
      \ }



"violet主题设置
nnoremap <silent> <Leader>b :call ToggleBackground()<CR>
function! ToggleBackground()
    if &background == "light"
        set background=dark
    else
        set background=light
    endif
endfunction
colorscheme violet


"vim-autopairs
let g:AutoPairs = {'(':')', '[':']', '{':'}',"'":"'",'"':'"'}     " 设置要自动配对的符号


"< F5> 编译和运行C++
map <f5> :call CompileRunGpp()<cr>
func! CompileRunGpp()
exec "w"
exec "!g++ % -o %<"
exec "! ./%<"
endfunc

colorscheme gruvbox
