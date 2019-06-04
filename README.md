# KS

All the stuff(docs and codes) about my happy days in KS

generate pdf

	pandoc x.md -o x.pdf --latex-engine=xelatex -V mainfont="SimSun"

using template to generate

	pandoc x.md --latex-engine=xelatex --template=chinese.template -o x.pdf
