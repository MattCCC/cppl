" C++L syntax: the C++ syntax plus the C++L-only surface from
" docs/GRAMMAR.md section 1. C++L is a superset of C++, so this sources the
" bundled C++ syntax first and only adds the contextual words on top.
"
" The words are contextual, so they are matched by the shape that gives them
" meaning rather than as bare keywords: `int verified = 0;` and
" `struct ghost {};` are ordinary C++ (tests/fixtures/contextual_identifiers.cpp)
" and must stay uncolored. `syntax match` is used throughout because
" `syntax keyword` cannot express a following context.

if exists("b:current_syntax")
  finish
endif

runtime! syntax/cpp.vim
unlet! b:current_syntax

" Declaration heads: `law name(`, `proof name(`, `type Name =`.
syntax match cpplKeyword "\<law\>\ze\s\+\h\w*\s*("
syntax match cpplKeyword "\<proof\>\ze\s\+\h\w*\s*("
syntax match cpplKeyword "\<type\>\ze\s\+\h\w*\s*[(=]"
syntax match cpplKeyword "\<proves\>"
syntax match cpplKeyword "\<where\>\ze\s*("

" Modifiers only ever prefix a further declaration.
syntax match cpplModifier "\<\%(verified\|pure\|ghost\|trusted\)\>\ze\s\+\%(law\|proof\|type\|struct\|class\|enum\|union\|auto\|void\|bool\|char\|short\|int\|long\|float\|double\|signed\|unsigned\|const\|constexpr\|consteval\|inline\|static\|template\|typename\)\>"
syntax match cpplModifier "\<\%(verified\|pure\|ghost\|trusted\)\>\ze\s\+\h[[:alnum:]_:]*[[:space:]*&<]\+[[:alpha:]_*&]"
syntax match cpplModifier "\<unsafe\>\ze\s*{"
syntax match cpplModifier "\<unsafe\>\ze\s\+\h"

" Specification clauses and quantifiers always take a parenthesized argument.
syntax match cpplClause "\<\%(expects\|ensures\|invariant\|decreases\)\>\ze\s*("
syntax match cpplQuantifier "\<\%(forall\|exists\)\>\ze\s*("
syntax match cpplContract "\<old\>\ze\s*("

" Proof statements (docs/GRAMMAR.md section 5).
syntax match cpplProof "\<\%(exact\|apply\|assume\|rewrite\|cases\|decompose\|induction\)\>\ze\s*[(<]"
syntax match cpplProof "\<refl\>\ze\s*;"

" A case omission, `omit label by contradiction evidence;` (section 5.7), is
" colored only as the whole form, which is never valid C++. Its words are
" ordinary names anywhere else, and a bare `contradiction name;` is spelled like
" a C++ declaration, so it is left to the C++ reading, as `ghost value;` is.
" cppl-lsp colors such a statement where the compiler recognized it as one, as
" a semantic token (lua/cppl/init.lua).
syntax match cpplOmission "\<omit\s\+\%(::\)\=\h\w*\%(\s*::\s*\h\w*\)*\%(\s*<[^;{}]*>\)\=\s\+by\s\+contradiction\>" contains=cpplOmissionWord
syntax match cpplOmissionWord "\<\%(omit\|by\|contradiction\)\>" contained

" `result` and `self` have meaning only inside a postcondition or a refinement
" predicate, so they are matched within that clause rather than everywhere.
syntax region cpplClauseBody matchgroup=cpplClause
      \ start="\<\%(ensures\|where\)\>\s*(" end=")"
      \ contains=cpplContractWord,cType,cNumber,cOperator transparent
syntax match cpplContractWord "\<\%(result\|self\)\>" contained

highlight default link cpplKeyword Keyword
highlight default link cpplModifier StorageClass
highlight default link cpplClause Keyword
highlight default link cpplQuantifier Keyword
highlight default link cpplProof Statement
highlight default link cpplOmissionWord Statement
highlight default link cpplContract Identifier
highlight default link cpplContractWord Identifier

let b:current_syntax = "cppl"
