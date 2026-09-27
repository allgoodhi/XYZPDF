#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Does the English catalogue still match what the code asks for?

    python tools/check_translations.py

Both directions of the same accident, which this project has now been caught by
twice (docs/store-submission.md sec.22 and sec.32-33):

* A string the code asks for that the catalogue never heard of -> the English UI
  shows Chinese, and nothing in the build says so. tools/add_language.py runs
  lupdate only when *creating* a catalogue, so nothing here keeps it current.
* A string the catalogue still carries that the code no longer asks for -> it
  looks fine and costs nothing, until `lupdate -no-obsolete` deletes it, and
  then it looks fine *and* is gone.

The second one is how a plural row nearly disappeared: lupdate decides whether a
call is translatable by the NAME it goes through, so a helper called `trN` is
invisible to it while `tr` is not. That is why src/edit/documenteditor.cpp
overloads `tr` instead of naming a second helper, and why this script compares
the sets rather than trusting either side.

A third direction turned up later, and the set comparison cannot see it at all: a
row whose (context, source) matches perfectly while its `<message numerus="yes">`
has been rewritten into a single flat translation, because lupdate read the call
by position and took the count for a disambiguation. Every row still looks
present; the English then says "Delete 1 pages" forever. plural_rules_of() checks
that shape, which is the part no named probe can be trusted to cover -- a probe
has to be added by whoever writes the row, and the whole point is the row nobody
thought about.

Compares (context, source) pairs: what lupdate extracts from the sources against
what the .ts files define. Exits non-zero on any difference, and prints both
sides of it. Line endings are normalised first: the repository stores these files
with LF, `core.autocrlf=true` checks them out with CRLF, and a multi-line <source>
keeps LF inside either way -- so a comparison that did not normalise would report
every wrapped sentence as missing.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TRANSLATIONS = os.path.join(ROOT, 'Resources', 'translations')
# Named rather than '.': lupdate would otherwise walk the build tree, where the
# same sources sit a second time with paths of their own.
SOURCES = ['main.cpp', 'mainwindow.cpp', 'documenttab.cpp', 'mainwindow.ui', 'src']
CATALOGUE_LANGS = ['en']


def find_lupdate():
    """The lupdate beside the Qt this project is built against."""
    explicit = os.environ.get('XYZPDF_LUPDATE')
    if explicit:
        return explicit
    found = shutil.which('lupdate')
    if found:
        return found
    for candidate in (r'E:\qt\6.11.2\msvc2022_64\bin\lupdate.exe',
                      r'C:\Qt\6.11.2\msvc2022_64\bin\lupdate.exe'):
        if os.path.exists(candidate):
            return candidate
    return None


def read(path):
    with open(path, 'rb') as handle:
        return handle.read().decode('utf-8', 'replace').replace('\r\n', '\n')


def pairs_of(text):
    """The (context, source) set a .ts defines."""
    out = set()
    for context in re.finditer(r'<context>\s*<name>([^<]*)</name>(.*?)</context>', text, re.S):
        name = context.group(1).strip()
        for source in re.finditer(r'<source>(.*?)</source>', context.group(2), re.S):
            out.add((name, source.group(1)))
    return out


def plural_rules_of(text, language):
    """Every numerus row of a .ts, checked against what the language needs.

    The set comparison above cannot see this failure: the (context, source) pair is
    still there, still matched, while the row has been rewritten from
    `<message numerus="yes">` with two `<numerusform>`s into one flat translation --
    which is what lupdate does when it decides the call handing it `%n` is not a
    plural call at all (it reads a tr() call by position, so a helper whose count
    sits in the disambiguation slot loses the flag). The English then says
    "Delete 1 pages" for every count, and every row still looks present.
    """
    problems = []
    # English agrees with a count by dropping the trailing "s", so the form picked
    # for one must not put a plural noun after it. "1 of those pages" stays legal
    # because the noun is not up against the number.
    #
    # What this does not catch, measured rather than assumed: one word between the
    # count and the noun ("1 ruled tables") walks past it. Widening the window to
    # "a couple of words and then a plural" reports correct sentences like "1 of
    # those files", so the window stays shut and the reworded rows are the ones to
    # write with the noun next to the number.
    reads_plural = re.compile(r'(?<![%\d])1\s+[a-z]{2,}s\b')
    for context in re.finditer(r'<context>\s*<name>([^<]*)</name>(.*?)</context>', text, re.S):
        name = context.group(1).strip()
        for message in re.finditer(r'<message([^>]*)>(.*?)</message>', context.group(2), re.S):
            attrs, body = message.group(1), message.group(2)
            source = re.search(r'<source>(.*?)</source>', body, re.S)
            if not source or '%n' not in source.group(1):
                continue
            where = '%s: %s' % (name, source.group(1).replace('\n', '\\n'))
            forms = re.findall(r'<numerusform>(.*?)</numerusform>', body, re.S)
            if 'numerus' not in attrs:
                problems.append('%s：源文里有 %%n，条目却不是 numerus 形状（%s 需要 %d 个形）'
                                % (where, language, 2 if language == 'en' else 1))
            elif len(forms) < 2:
                problems.append('%s：numerus 条目只有 %d 个 <numerusform>' % (where, len(forms)))
            elif forms[0] == forms[1]:
                problems.append('%s：两个形一模一样，等于没有单数形' % where)
            elif reads_plural.search(forms[0].replace('%n', '1')):
                problems.append('%s：单数形按多数写：%s' % (where, forms[0]))
    return problems


def paragraph_break_rules_of(text):
    """Every row, checked that the translation keeps the whitespace the source opens with.

    Several summaries in this program are built with +=, and the paragraph break that
    separates an aside from the arithmetic lives *inside* the translatable literal:
    `tr("\n\n这份文档里已经有 %n 页是合并进来的了。")`. An English form that drops those two
    newlines still matches by (context, source), still carries %n, still has two distinct
    forms -- and renders as two sentences welded together. That is how this rule was found:
    reading the merge box on screen in English gave
    `…they end up together at pages 42-43.This document already has 1 page merged in…`,
    while the same dialog in Chinese broke the paragraph correctly. 4 of the 35 forms that
    open with a break had lost it; the other 31 are why equality (not "at least one") is the
    test that has no false positives here.
    """
    def leading_newlines(value):
        return re.match(r'[ \t\r\n]*', value).group(0).count('\n')

    problems = []
    for context in re.finditer(r'<context>\s*<name>([^<]*)</name>(.*?)</context>', text, re.S):
        name = context.group(1).strip()
        for message in re.finditer(r'<message([^>]*)>(.*?)</message>', context.group(2), re.S):
            attrs, body = message.group(1), message.group(2)
            source = re.search(r'<source>(.*?)</source>', body, re.S)
            if not source:
                continue
            want = leading_newlines(source.group(1))
            if want == 0:
                continue
            where = '%s: %s' % (name, source.group(1).strip().replace('\n', '\\n'))
            forms = (re.findall(r'<numerusform>(.*?)</numerusform>', body, re.S)
                     if 'numerus' in attrs else
                     [re.search(r'<translation[^>]*>(.*?)</translation>', body, re.S).group(1)])
            for form in forms:
                if leading_newlines(form) != want:
                    problems.append('%s：源文开头有 %d 个换行，译文只有 %d 个，两句话会粘在一起：%s'
                                    % (where, want, leading_newlines(form), form.strip()[:60]))
    return problems


def main():
    lupdate = find_lupdate()
    if not lupdate:
        print('找不到 lupdate。把 Qt 的 bin 加进 PATH，或用 XYZPDF_LUPDATE 指全路径。')
        return 2

    out = []
    for lang in CATALOGUE_LANGS:
        catalogue = os.path.join(TRANSLATIONS, 'xyzpdf_%s.ts' % lang)
        if not os.path.exists(catalogue):
            print('没有 %s 这个目录文件。' % os.path.relpath(catalogue, ROOT))
            return 2
        # -no-obsolete only to make the extraction a pure "what does the code ask
        # for"; nothing is written back, the target is a scratch file. Named, not
        # created: lupdate reads an existing .ts before writing it, and an empty
        # placeholder is a parse error ("Premature end of document").
        scratch = os.path.join(tempfile.mkdtemp(prefix='xyzpdf-lv-'), 'xyzpdf_%s.ts' % lang)
        command = [lupdate, '-no-obsolete', '-source-language', 'zh_CN',
                   '-target-language', lang] + SOURCES + ['-ts', scratch]
        outcome = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT)
        if outcome.returncode != 0:
            print(outcome.stdout.decode('utf-8', 'replace'))
            shutil.rmtree(os.path.dirname(scratch), ignore_errors=True)
            return 2
        asked = pairs_of(read(scratch))
        shutil.rmtree(os.path.dirname(scratch), ignore_errors=True)
        catalogue_text = read(catalogue)
        listed = pairs_of(catalogue_text)
        for context, source in sorted(asked - listed):
            out.append('%s: 代码在问，目录里没有：%s' % (context, source.replace('\n', '\\n')))
        for context, source in sorted(listed - asked):
            out.append('%s: 目录里有，代码已经不问了：%s' % (context, source.replace('\n', '\\n')))
        out += plural_rules_of(catalogue_text, lang)
        out += paragraph_break_rules_of(catalogue_text)
        plural = len([1 for line in catalogue_text.splitlines() if '<message numerus' in line])
        print('%s.ts：代码 %d 条，目录 %d 条，其中 numerus %d 条' % (lang, len(asked), len(listed),
                                                            plural))

    if out:
        for line in out:
            print('  ' + line)
        print('两边不一致，上面就是差集。')
        return 1
    print('代码在问的每一条，目录里都有；目录里也没有不再被问的。')
    return 0


if __name__ == '__main__':
    sys.exit(main())
