// The language table LANGS (file detection, names, keywords for the fallback lexer).

#include "languages.h"

#include <cctype>
#include <cstring>

#include "utf8.h"

#define KW_C "auto break case const continue default do else enum extern for goto if inline register restrict " \
             "return sizeof static struct switch typedef union volatile while _Alignas _Alignof _Atomic _Generic " \
             "_Noreturn _Static_assert _Thread_local "
#define TY_C "char double float int long short signed unsigned void bool _Bool size_t ssize_t int8_t int16_t " \
             "int32_t int64_t uint8_t uint16_t uint32_t uint64_t intptr_t uintptr_t ptrdiff_t wchar_t FILE NULL " \
             "true false EOF stdin stdout stderr "
#define KW_JS "async await break case catch class const continue debugger default delete do else export extends " \
              "finally for from function get if import in instanceof let new of return set static super switch " \
              "this throw try typeof var void while with yield "
#define TY_JS "true false null undefined NaN Infinity Array Object String Number Boolean Symbol BigInt Promise " \
              "Map Set WeakMap Date RegExp Error JSON Math console window document globalThis require module "

const LangDef LANGS[] = {
    {"C", ".c .h", "", LK_CODE, F_PREPROC, "//", "", "/* */", "", "\"'", "", KW_C, TY_C},
    {"C++", ".cpp .cc .cxx .c++ .hpp .hh .hxx .h++ .ipp .tpp .ino .mm .cu .glsl .vert .frag .geom .comp .hlsl", "",
     LK_CODE, F_PREPROC, "//", "", "/* */", "", "\"'", "",
     KW_C "alignas alignof and asm bitand bitor catch class compl concept consteval constexpr constinit const_cast "
          "co_await co_return co_yield decltype delete dynamic_cast explicit export final friend mutable namespace "
          "new noexcept not operator or override private protected public reinterpret_cast requires static_assert "
          "static_cast template this thread_local throw try typeid typename using virtual xor nullptr",
     TY_C "std string string_view vector map set unordered_map unordered_set array deque list pair tuple optional "
          "variant unique_ptr shared_ptr weak_ptr function char8_t char16_t char32_t nullptr_t"},
    {"C#", ".cs .csx", "", LK_CODE, F_PREPROC | F_DECOR, "//", "", "/* */", "", "\"'", "",
     "abstract as base break case catch checked class const continue default delegate do else enum event explicit "
     "extern finally fixed for foreach goto if implicit in interface internal is lock namespace new operator out "
     "override params private protected public readonly ref return sealed sizeof stackalloc static struct switch "
     "this throw try typeof unchecked unsafe using virtual volatile while async await var yield get set add remove "
     "partial where when record init with nameof global required file",
     "bool byte char decimal double float int long object sbyte short string uint ulong ushort void dynamic nint "
     "nuint null true false String Object Console List Dictionary Task Exception"},
    {"Java", ".java", "", LK_CODE, F_DECOR, "//", "", "/* */", "", "\"'", "\"\"\" \"\"\" 1",
     "abstract assert break case catch class const continue default do else enum extends final finally for goto if "
     "implements import instanceof interface native new package private protected public return static strictfp "
     "super switch synchronized this throw throws transient try volatile while var record sealed permits yield def",
     "boolean byte char double float int long short void null true false String Object Integer Long Double "
     "Boolean Character List ArrayList Map HashMap Set HashSet System Exception"},
    {"Groovy", ".groovy .gradle", "groovy", LK_CODE, F_DECOR, "//", "", "/* */", "", "\"'", "\"\"\" \"\"\" 1",
     "abstract assert break case catch class const continue default do else enum extends final finally for goto if "
     "implements import instanceof interface native new package private protected public return static strictfp "
     "super switch synchronized this throw throws transient try volatile while var record sealed permits yield def",
     "boolean byte char double float int long short void null true false String Object Integer Long Double "
     "Boolean Character List ArrayList Map HashMap Set HashSet System Exception"},
    {"Kotlin", ".kt .kts", "", LK_CODE, F_DECOR, "//", "", "/* */", "", "\"'", "\"\"\" \"\"\" 0",
     "as break class continue do else for fun if in interface is object package return super this throw try "
     "typealias typeof val var when while by catch constructor delegate dynamic field file finally get import "
     "init param property receiver set setparam where actual abstract annotation companion const crossinline data "
     "enum expect external final infix inline inner internal lateinit noinline open operator out override private "
     "protected public reified sealed suspend tailrec vararg value",
     "Any Boolean Byte Char Double Float Int Long Nothing Short String Unit Array List MutableList Map "
     "MutableMap Set null true false it println"},
    {"Swift", ".swift", "", LK_CODE, F_DECOR | F_PREPROC, "//", "", "/* */", "", "\"", "\"\"\" \"\"\" 1",
     "associatedtype class deinit enum extension fileprivate func import init inout internal let open operator "
     "private protocol public rethrows static struct subscript typealias var break case continue default defer do "
     "else fallthrough for guard if in repeat return switch where while as catch is super self Self throw throws "
     "try async await some any mutating nonmutating lazy weak unowned override final required convenience",
     "Int Int8 Int16 Int32 Int64 UInt Double Float Bool String Character Array Dictionary Set Optional Void Any "
     "AnyObject nil true false print"},
    {"Scala", ".scala .sc", "scala", LK_CODE, F_DECOR, "//", "", "/* */", "", "\"'", "\"\"\" \"\"\" 0",
     "abstract case catch class def do else extends final finally for forSome if implicit import lazy match new "
     "object override package private protected return sealed super this throw trait try type val var while with "
     "yield given using enum then export end",
     "Int Long Double Float Boolean Char String Unit Any AnyRef Nothing Option Some None List Seq Map Set Vector "
     "null true false println"},
    {"Dart", ".dart", "dart", LK_CODE, F_DECOR, "//", "", "/* */", "", "\"'", "\"\"\" \"\"\" 1 ''' ''' 1",
     "abstract as assert async await break case catch class const continue covariant default deferred do dynamic "
     "else enum export extends extension external factory final finally for get hide if implements import in "
     "interface is late library mixin new on operator part required rethrow return sealed set show static super "
     "switch sync this throw try typedef var void when while with yield base",
     "int double num bool String List Map Set Iterable Future Stream Object Function Never null true false print"},
    {"Go", ".go", "", LK_CODE, 0, "//", "", "/* */", "", "\"'", "` ` 0",
     "break case chan const continue default defer else fallthrough for func go goto if import interface map "
     "package range return select struct switch type var",
     "bool byte complex64 complex128 error float32 float64 int int8 int16 int32 int64 rune string uint uint8 "
     "uint16 uint32 uint64 uintptr any comparable true false nil iota append cap clear close copy delete len make "
     "max min new panic print println recover"},
    {"Rust", ".rs", "", LK_CODE, F_CHARSTRICT, "//", "", "/* */", "", "\"'", "",
     "as async await break const continue crate dyn else enum extern fn for if impl in let loop match mod move mut "
     "pub ref return self Self static struct super trait type unsafe use where while macro_rules union yield",
     "bool char str u8 u16 u32 u64 u128 usize i8 i16 i32 i64 i128 isize f32 f64 String Vec Option Some None "
     "Result Ok Err Box Rc Arc RefCell Cell HashMap HashSet BTreeMap true false println print format vec panic "
     "assert assert_eq"},
    {"Zig", ".zig .zon", "", LK_CODE, 0, "//", "", "", "", "\"'", "",
     "addrspace align allowzero and anyframe anytype asm async await break callconv catch comptime const continue "
     "defer else enum errdefer error export extern fn for if inline linksection noalias noinline nosuspend opaque "
     "or orelse packed pub resume return struct suspend switch test threadlocal try union unreachable "
     "usingnamespace var volatile while",
     "bool void type anyerror anyopaque noreturn i8 i16 i32 i64 i128 u8 u16 u32 u64 u128 usize isize f16 f32 f64 "
     "f128 c_int comptime_int comptime_float null undefined true false"},
    {"JavaScript", ".js .mjs .cjs .jsx", "node nodejs deno bun", LK_CODE, F_DECOR, "//", "", "/* */", "", "\"'",
     "` ` 1", KW_JS, TY_JS},
    {"TypeScript", ".ts .mts .cts", "", LK_CODE, F_DECOR, "//", "", "/* */", "", "\"'", "` ` 1",
     KW_JS "abstract as declare enum implements interface keyof namespace private protected public readonly type "
           "is infer satisfies override accessor",
     TY_JS "any boolean number string unknown never object symbol bigint Record Partial Readonly"},
    {"TSX", ".tsx", "", LK_CODE, F_DECOR, "//", "", "/* */", "", "\"'", "` ` 1",
     KW_JS "abstract as declare enum implements interface keyof namespace private protected public readonly type "
           "is infer satisfies override accessor",
     TY_JS "any boolean number string unknown never object symbol bigint Record Partial Readonly"},
    {"Python", ".py .pyw .pyi .pyx .bzl", "python python2 python3 pypy pypy3", LK_CODE, F_DECOR, "#", "", "", "", "\"'",
     "\"\"\" \"\"\" 1 ''' ''' 1",
     "and as assert async await break class continue def del elif else except finally for from global if import "
     "in is lambda nonlocal not or pass raise return try while with yield match case type",
     "True False None self cls int float complex str bool bytes bytearray list dict set frozenset tuple object "
     "print len range enumerate zip map filter open type isinstance issubclass super property staticmethod "
     "classmethod sorted reversed min max sum abs any all iter next input repr hasattr getattr setattr "
     "Exception ValueError TypeError KeyError IndexError RuntimeError __name__ __init__ __main__"},
    {"Ruby", ".rb .rake .gemspec .ru .erb Gemfile Rakefile Vagrantfile Podfile", "ruby", LK_CODE,
     F_ATVAR | F_SYMBOL | F_DOLLAR, "#", "", "=begin =end", "", "\"'`", "",
     "BEGIN END alias and begin break case class def defined do else elsif end ensure for if in module next not or "
     "redo rescue retry return self super then undef unless until when while yield require require_relative "
     "include extend attr_accessor attr_reader attr_writer private protected public lambda proc raise loop",
     "true false nil puts print p Integer Float String Array Hash Symbol Proc Struct Kernel Object"},
    {"Perl", ".pl .pm .t .pod", "perl", LK_CODE, F_DOLLAR | F_ATVAR, "#", "", "", "", "\"'", "",
     "my our local sub if elsif else unless while until for foreach do last next redo return use no require "
     "package BEGIN END and or not eq ne lt gt le ge cmp qw qq print printf say die warn eval undef defined exists "
     "delete shift push pop unshift splice keys values each map grep sort reverse join split open close chomp "
     "chop length substr index sprintf lc uc ref bless local wantarray",
     "STDIN STDOUT STDERR ARGV ENV __FILE__ __LINE__ __PACKAGE__ __DATA__ __END__ strict warnings"},
    {"PHP", ".php .phtml .php3 .php4 .php5 .phps", "php", LK_CODE, F_DOLLAR | F_DECOR, "// #", "", "/* */", "", "\"'",
     "",
     "abstract and array as break callable case catch class clone const continue declare default do echo else "
     "elseif empty enddeclare endfor endforeach endif endswitch endwhile enum extends final finally fn for foreach "
     "function global goto if implements include include_once instanceof insteadof interface isset list match "
     "namespace new or print private protected public readonly require require_once return static switch throw "
     "trait try unset use var while xor yield",
     "true false null TRUE FALSE NULL int float string bool void mixed never self parent object iterable array "
     "this"},
    {"Lua", ".lua .rockspec", "lua luajit", LK_CODE, 0, "--", "", "--[[ ]]", "", "\"'", "[[ ]] 0",
     "and break do else elseif end for function goto if in local not or repeat return then until while",
     "true false nil self _G _ENV print pairs ipairs next select type tostring tonumber assert error pcall xpcall "
     "require setmetatable getmetatable rawget rawset table string math io os coroutine utf8"},
    {"Shell",
     ".sh .bash .zsh .ksh .ash .command .bashrc .bash_profile .bash_logout .bash_aliases .profile .zshrc "
     ".zprofile .zshenv .zlogin .xinitrc .xprofile PKGBUILD APKBUILD .envrc",
     "sh bash zsh ksh dash ash mksh", LK_CODE, F_DOLLAR | F_HASHSPACE, "#", "", "", "", "", "\" \" 1 ' ' 0 ` ` 1",
     "if then else elif fi case esac for select while until do done in function time coproc return exit break "
     "continue local export readonly declare typeset unset shift source alias unalias eval exec trap set let",
     "echo printf read cd pwd test true false command builtin type which kill wait getopts mapfile pushd popd "
     "dirs umask"},
    {"Fish", ".fish", "fish", LK_CODE, F_DOLLAR | F_HASHSPACE, "#", "", "", "", "", "\" \" 1 ' ' 0",
     "and begin break builtin case command continue else end exit for function if in not or return set switch "
     "while argparse source functions emit",
     "echo printf read cd test true false string math status contains abbr alias count set_color type"},
    {"PowerShell", ".ps1 .psm1 .psd1", "pwsh powershell", LK_CODE, F_NOCASE | F_DOLLAR | F_DASHID | F_NOESC, "#", "",
     "<# #>", "", "\"'", "",
     "begin break catch class continue data define do dynamicparam else elseif end enum exit filter finally for "
     "foreach from function hidden if in param process return static switch throw trap try until using var "
     "while workflow parallel sequence",
     "write-host write-output write-error write-warning get-item set-item get-childitem get-content set-content "
     "new-item remove-item copy-item move-item get-process get-service where-object select-object foreach-object "
     "sort-object import-module invoke-command invoke-webrequest test-path join-path"},
    {"Batch", ".bat .cmd .btm", "", LK_CODE, F_NOCASE | F_NOESC | F_PERCENT | F_BATLABEL, "", "rem :: @rem", "", "",
     "\"", "",
     "echo set if else goto call exit for in do not exist defined errorlevel setlocal endlocal shift pause cls cd "
     "chdir pushd popd start choice title color del erase copy xcopy move ren rename mkdir md rd rmdir type equ "
     "neq lss leq gtr geq off on nul ver path prompt enabledelayedexpansion enableextensions",
     ""},
    {"BASIC", ".bas .bi .bm .vb .vbs .frm .cls", "", LK_CODE, F_NOCASE | F_NOESC, "'", "rem", "", "", "\"", "",
     "and as call case close const declare def defint defsng defdbl deflng defstr dim do else elseif end endif "
     "exit for function get gosub goto if input is let line local loop mod next not on open or print put "
     "randomize read redim resume return select shared sleep static step stop sub swap then to type until wend "
     "while width write xor byref byval private public class module imports namespace new nothing of option "
     "explicit property set with each in error cls color locate screen beep data restore",
     "integer long single double string boolean byte variant object date currency true false chr$ str$ val len "
     "left$ right$ mid$ inkey$ rnd int abs sqr"},
    {"Pascal", ".pas .pp .dpr .dpk .lpr .inc", "", LK_CODE, F_NOCASE | F_NOESC, "//", "", "{ }", "(* *)", "'", "",
     "and array as asm begin case class const constructor destructor div do downto else end except exports file "
     "finalization finally for function goto if implementation in inherited initialization inline interface is "
     "label library mod nil not object of or out packed procedure program property raise record repeat "
     "resourcestring set shl shr then threadvar to try type unit until uses var while with xor private protected "
     "public published override virtual abstract overload forward",
     "integer cardinal shortint smallint longint int64 byte word longword char boolean real single double "
     "extended string ansistring widestring pointer true false writeln write readln read self result exit"},
    {"Fortran", ".f .for .f77 .f90 .f95 .f03 .f08", "", LK_CODE, F_NOCASE | F_NOESC, "!", "", "", "", "\"'", "",
     "program end subroutine function module use implicit none parameter dimension allocatable intent in out "
     "inout if then else elseif endif do enddo while call return stop contains type select case default print "
     "write read open close format allocate deallocate result recursive pure elemental interface public private "
     "save data common include go to exit cycle",
     "integer real double precision complex logical character true false"},
    {"Haskell", ".hs .lhs", "runghc runhaskell", LK_CODE, F_CHARSTRICT, "--", "", "{- -}", "", "\"'", "",
     "case class data default deriving do else forall if import in infix infixl infixr instance let module "
     "newtype of qualified then type where as hiding family",
     "Int Integer Float Double Bool Char String Maybe Just Nothing Either Left Right IO True False Show Eq Ord "
     "Functor Monad map filter foldr foldl putStrLn print return pure"},
    {"Lisp", ".el .fnl .lsp", "sbcl clisp guile racket", LK_CODE, F_LISP, ";", "", "#| |#", "", "\"", "",
     "defun defmacro defvar defparameter defconstant defclass defmethod defgeneric defstruct defn defn- def "
     "defmulti defprotocol defrecord let let* letrec flet labels lambda fn if when unless cond case and or not "
     "progn prog1 do dolist dotimes loop setq setf quote require ns define define-syntax begin else import "
     "use-package",
     "nil t true false car cdr cons list first rest map filter reduce apply funcall format print println"},
    {"Common Lisp", ".lisp .cl .asd", "sbcl clisp", LK_CODE, F_LISP, ";", "", "#| |#", "", "\"", "",
     "defun defmacro defvar defparameter defconstant defclass defmethod defgeneric defstruct defn defn- def "
     "defmulti defprotocol defrecord let let* letrec flet labels lambda fn if when unless cond case and or not "
     "progn prog1 do dolist dotimes loop setq setf quote require ns define define-syntax begin else import "
     "use-package",
     "nil t true false car cdr cons list first rest map filter reduce apply funcall format print println"},
    {"Clojure", ".clj .cljs .cljc .edn .bb", "clojure bb", LK_CODE, F_LISP, ";", "", "#| |#", "", "\"", "",
     "defun defmacro defvar defparameter defconstant defclass defmethod defgeneric defstruct defn defn- def "
     "defmulti defprotocol defrecord let let* letrec flet labels lambda fn if when unless cond case and or not "
     "progn prog1 do dolist dotimes loop setq setf quote require ns define define-syntax begin else import "
     "use-package",
     "nil t true false car cdr cons list first rest map filter reduce apply funcall format print println"},
    {"Scheme", ".scm .ss .rkt .sld", "guile racket", LK_CODE, F_LISP, ";", "", "#| |#", "", "\"", "",
     "defun defmacro defvar defparameter defconstant defclass defmethod defgeneric defstruct defn defn- def "
     "defmulti defprotocol defrecord let let* letrec flet labels lambda fn if when unless cond case and or not "
     "progn prog1 do dolist dotimes loop setq setf quote require ns define define-syntax begin else import "
     "use-package",
     "nil t true false car cdr cons list first rest map filter reduce apply funcall format print println"},
    {"Erlang", ".erl .hrl", "escript", LK_CODE, 0, "%", "", "", "", "\"", "",
     "after and andalso band begin bnot bor bsl bsr bxor case catch cond div end fun if let not of or orelse "
     "receive rem try when xor maybe else",
     "true false undefined ok error module export import define record include spawn self"},
    {"Elixir", ".ex .exs .heex", "elixir", LK_CODE, F_SYMBOL | F_DECOR, "#", "", "", "", "\"'", "\"\"\" \"\"\" 1",
     "def defp defmodule defstruct defmacro defmacrop defimpl defprotocol defdelegate defguard defexception do "
     "end if else unless case cond fn when with for try catch rescue after raise receive import require alias use "
     "quote unquote and or not in",
     "true false nil Enum List Map String IO Kernel Agent Task GenServer"},
    {"R", ".r .rmd", "Rscript", LK_CODE, 0, "#", "", "", "", "\"'", "",
     "if else repeat while function for in next break return library require switch",
     "TRUE FALSE NULL NA NA_integer_ NA_real_ NA_character_ Inf NaN c list data.frame print paste cat"},
    {"Julia", ".jl", "julia", LK_CODE, F_DECOR, "#", "", "#= =#", "", "\"", "\"\"\" \"\"\" 1",
     "function end if elseif else for while return module baremodule using import export struct mutable "
     "abstract primitive type begin let do try catch finally macro quote const local global break continue in "
     "where",
     "true false nothing missing Int Int64 Float64 String Bool Char Vector Matrix Array Dict Set Tuple Any "
     "println print"},
    {"SQL", ".sql .ddl .dml .psql .mysql", "", LK_CODE, F_NOCASE | F_NOESC, "--", "", "/* */", "", "'\"`", "",
     "select from where and or not insert into values update set delete create table drop alter add column index "
     "view primary key foreign references join left right inner outer full cross on as group by order having "
     "limit offset distinct union all exists in is null like ilike between case when then else end begin commit "
     "rollback transaction grant revoke database schema if unique default check constraint asc desc returning "
     "with recursive trigger function procedure declare cast replace temporary temp truncate explain analyze "
     "vacuum",
     "int integer bigint smallint tinyint serial bigserial float real double precision decimal numeric varchar "
     "char text date time timestamp timestamptz interval boolean bool blob bytea json jsonb uuid count sum avg "
     "min max coalesce nullif now current_date current_timestamp true false"},
    {"CSS", ".css .sass .less .styl", "", LK_CODE, F_DASHID | F_HEXCOLOR | F_DECOR, "", "", "/* */", "", "\"'", "",
     "color background background-color background-image border border-radius border-color margin padding "
     "display position top left right bottom width height font font-size font-weight font-family font-style "
     "line-height text-align text-decoration flex flex-direction flex-wrap grid grid-template-columns z-index "
     "opacity overflow transition transform animation content cursor float clear box-sizing box-shadow "
     "max-width min-width max-height min-height align-items align-content justify-content gap outline visibility "
     "white-space vertical-align list-style",
     "none auto inherit initial unset block inline inline-block inline-flex absolute relative fixed sticky "
     "static solid dashed dotted hidden visible bold normal italic center important transparent currentColor "
     "rgb rgba hsl hsla var calc url"},
    {"SCSS", ".scss", "", LK_CODE, F_DASHID | F_HEXCOLOR | F_DECOR, "", "", "/* */", "", "\"'", "",
     "color background background-color background-image border border-radius border-color margin padding "
     "display position top left right bottom width height font font-size font-weight font-family font-style "
     "line-height text-align text-decoration flex flex-direction flex-wrap grid grid-template-columns z-index "
     "opacity overflow transition transform animation content cursor float clear box-sizing box-shadow "
     "max-width min-width max-height min-height align-items align-content justify-content gap outline visibility "
     "white-space vertical-align list-style",
     "none auto inherit initial unset block inline inline-block inline-flex absolute relative fixed sticky "
     "static solid dashed dotted hidden visible bold normal italic center important transparent currentColor "
     "rgb rgba hsl hsla var calc url"},
    {"JSON", ".json .jsonc .json5 .geojson .webmanifest .babelrc .eslintrc .prettierrc", "", LK_CODE, F_KEYSTR, "//",
     "", "/* */", "", "\"", "", "", "true false null"},
    {"HTML", ".html .htm .xhtml .shtml .vue .svelte", "", LK_MARKUP, 0, "", "", "", "", "", "", "", ""},
    {"XML",
     ".xml .svg .xsd .xsl .xslt .plist .xaml .csproj .vbproj .vcxproj .props .targets .rss .atom .ui .glade .kml .gpx",
     "", LK_MARKUP, 0, "", "", "", "", "", "", "", ""},
    {"Markdown", ".md .markdown .mdown .mkd .mdx", "", LK_MARKDOWN, 0, "", "", "", "", "", "", "", ""},
    {"INI",
     ".ini .cfg .conf .cnf .properties .desktop .service .timer .socket .mount .target .path .editorconfig .gitconfig "
     ".gitmodules .env .npmrc .pylintrc .flake8 .reg .inf",
     "", LK_INI, 0, "", "", "", "", "", "", "", ""},
    {"TOML", ".toml Cargo.lock Pipfile", "", LK_INI, 0, "", "", "", "", "", "", "", ""},
    {"YAML", ".yml .yaml", "", LK_YAML, 0, "", "", "", "", "", "", "", ""},
    {"Diff", ".diff .patch .rej", "", LK_DIFF, 0, "", "", "", "", "", "", "", ""},
    {"Makefile", ".mk .mak .make Makefile makefile GNUmakefile Kbuild", "make", LK_CODE,
     F_DOLLAR | F_MAKE | F_LABELS | F_NOESC, "#", "", "", "", "\"'", "",
     "ifeq ifneq ifdef ifndef else endif include sinclude define endef export unexport override vpath",
     "wildcard patsubst subst filter filter-out foreach shell notdir basename dir addprefix addsuffix sort "
     "strip findstring word words call eval origin info warning error .PHONY .SUFFIXES .DEFAULT"},
    {"CMake", ".cmake CMakeLists.txt", "", LK_CODE, F_NOCASE | F_DOLLAR, "#", "", "", "", "\"", "",
     "if else elseif endif foreach endforeach while endwhile function endfunction macro endmacro return break "
     "continue block endblock set unset option project cmake_minimum_required add_executable add_library "
     "target_link_libraries target_include_directories target_compile_options target_compile_definitions "
     "target_compile_features target_sources include_directories link_directories find_package find_library "
     "find_program message install add_subdirectory include list string file configure_file add_definitions "
     "add_custom_command add_custom_target add_test enable_testing set_target_properties",
     "on off true false yes no and or not public private interface required"},
    {"Dockerfile", ".dockerfile Dockerfile Containerfile", "", LK_CODE, F_NOCASE | F_DOLLAR, "#", "", "", "", "\"'", "",
     "from run cmd label maintainer expose env add copy entrypoint volume user workdir arg onbuild stopsignal "
     "healthcheck shell as",
     ""},
    {"Assembler", ".s .masm .a51 .z80 .6502", "", LK_CODE, F_NOCASE | F_FIRSTWORD | F_LABELS | F_DOTID | F_HASHSPACE,
     "; #", "", "/* */", "", "\"'", "",
     "section segment global globl extern db dw dd dq dt resb resw resd resq equ times bits org align use16 "
     "use32 use64 .text .data .bss .rodata .global .globl .section .byte .word .long .quad .ascii .asciz "
     ".string .equ .set .macro .endm .align .type .size .file .intel_syntax %define %macro %endmacro %include "
     "%ifdef %endif %if %else proc endp end model",
     "eax ebx ecx edx esi edi esp ebp rax rbx rcx rdx rsi rdi rsp rbp r8 r9 r10 r11 r12 r13 r14 r15 ax bx cx dx "
     "si di sp bp al ah bl bh cl ch dl dh cs ds es fs gs ss ip eip rip xmm0 xmm1 xmm2 xmm3 xmm4 xmm5 xmm6 xmm7 "
     "byte word dword qword ptr short near far"},
    {"NASM", ".asm .nasm", "", LK_CODE, F_NOCASE | F_FIRSTWORD | F_LABELS | F_DOTID | F_HASHSPACE, "; #", "", "/* */",
     "", "\"'", "",
     "section segment global globl extern db dw dd dq dt resb resw resd resq equ times bits org align use16 "
     "use32 use64 .text .data .bss .rodata .global .globl .section .byte .word .long .quad .ascii .asciz "
     ".string .equ .set .macro .endm .align .type .size .file .intel_syntax %define %macro %endmacro %include "
     "%ifdef %endif %if %else proc endp end model",
     "eax ebx ecx edx esi edi esp ebp rax rbx rcx rdx rsi rdi rsp rbp r8 r9 r10 r11 r12 r13 r14 r15 ax bx cx dx "
     "si di sp bp al ah bl bh cl ch dl dh cs ds es fs gs ss ip eip rip xmm0 xmm1 xmm2 xmm3 xmm4 xmm5 xmm6 xmm7 "
     "byte word dword qword ptr short near far"},
    {"TeX", ".tex .sty .cls .ltx .dtx", "", LK_CODE, F_BSCMD | F_NOESC, "%", "", "", "", "$", "", "", ""},
    {"BibTeX", ".bib", "", LK_CODE, F_BSCMD | F_NOESC, "%", "", "", "", "$", "", "", ""},
    {"Terraform", ".tf .tfvars", "", LK_CODE, 0, "# //", "", "/* */", "", "\"", "",
     "resource data variable output module provider locals terraform backend required_providers for_each count "
     "depends_on lifecycle dynamic content if for in job group task",
     "true false null string number bool list map set object tuple any"},
    {"HCL", ".hcl .nomad", "", LK_CODE, 0, "# //", "", "/* */", "", "\"", "",
     "resource data variable output module provider locals terraform backend required_providers for_each count "
     "depends_on lifecycle dynamic content if for in job group task",
     "true false null string number bool list map set object tuple any"},
    {"Nix", ".nix", "", LK_CODE, 0, "#", "", "/* */", "", "\"", "'' '' 0",
     "let in with rec inherit if then else assert import or",
     "true false null builtins pkgs lib stdenv fetchurl fetchFromGitHub mkDerivation"},
};

#undef KW_C
#undef TY_C
#undef KW_JS
#undef TY_JS

const int NLANGS = int(sizeof LANGS / sizeof *LANGS);

std::vector<u32> splitWords(const char* s) {
    std::vector<u32> r;
    u32 all = U(s), cur;
    for (char32_t c : all) {
        if (c == ' ') {
            if (!cur.empty()) r.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) r.push_back(cur);
    return r;
}

u32 lower(u32 s) {
    for (auto& c : s) c = fold(c);
    return s;
}

// Detects the language from file name and "#!" line; -1 = none
int detectLang(const std::string& path, const u32& firstLine) {
    std::string base = path;
    size_t sl = base.rfind('/');
    if (sl != std::string::npos) base = base.substr(sl + 1);
    std::string ext;
    size_t dot = base.rfind('.');
    if (dot != std::string::npos) ext = base.substr(dot);
    for (auto& c : ext) c = char(tolower((unsigned char)c));
    std::string lbase = base;
    for (auto& c : lbase) c = char(tolower((unsigned char)c));
    if (!base.empty()) {
        for (int i = 0; i < NLANGS; i++) {  // exact file names first, then extensions
            for (auto& w : splitWords(LANGS[i].files)) {
                std::string f = toUtf8(w);
                if (f == base || (f[0] == '.' && f == lbase)) return i;
            }
        }
        if (!ext.empty())
            for (int i = 0; i < NLANGS; i++)
                for (auto& w : splitWords(LANGS[i].files))
                    if (toUtf8(w) == ext) return i;
        if (lbase.size() > 10 && lbase.compare(0, 10, "dockerfile") == 0) {
            for (int i = 0; i < NLANGS; i++)
                if (strcmp(LANGS[i].name, "Dockerfile") == 0) return i;
        }
    }
    // Shebang
    if (firstLine.size() > 2 && firstLine[0] == '#' && firstLine[1] == '!') {
        std::vector<u32> parts = splitWords(toUtf8(firstLine.substr(2)).c_str());
        std::string prog;
        for (size_t k = 0; k < parts.size(); k++) {
            std::string p = toUtf8(parts[k]);
            size_t s = p.rfind('/');
            if (s != std::string::npos) p = p.substr(s + 1);
            if (p == "env" || (!p.empty() && p[0] == '-')) continue;
            prog = p;
            break;
        }
        while (!prog.empty() && (isdigit((unsigned char)prog.back()) || prog.back() == '.')) prog.pop_back();
        if (!prog.empty())
            for (int i = 0; i < NLANGS; i++)
                for (auto& w : splitWords(LANGS[i].shebang)) {
                    std::string s = toUtf8(w);
                    while (!s.empty() && isdigit((unsigned char)s.back())) s.pop_back();
                    if (s == prog) return i;
                }
    }
    return -1;
}
