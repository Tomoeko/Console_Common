#!/usr/bin/env python3
"""Decide whether a revision changes build inputs, using only the standard library."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tokenize
from io import StringIO

DOCUMENT_SUFFIXES = {'.md', '.markdown', '.rst', '.txt', '.adoc'}
C_SUFFIXES = {'.c', '.h', '.m', '.mm', '.glsl', '.vert', '.frag'}
C_TOKENS = re.compile(
    r'(?:u8|[LuU])?"(?:\\.|[^"\\])*"|(?:[LuU])?\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*'
    r'|\s+|[A-Za-z_][A-Za-z_0-9]*|(?:\d|\.\d)(?:[eEpP][+-]|[\w.])*'
    r'|>>=|<<=|\.\.\.|->|\+\+|--|&&|\|\||[<>!=+*/%&|^\-]=|<<|>>|.',
    re.DOTALL,
)


def relevant(path):
    name = Path(path).name
    if name == 'CMakeLists.txt':
        return True
    if Path(path).suffix.lower() in DOCUMENT_SUFFIXES:
        return False
    if path.startswith(('Files/', 'docs/')):
        return False
    return True


def source_tokens(path, content):
    text = content.decode('utf-8')
    suffix = Path(path).suffix.lower()
    if suffix in C_SUFFIXES:
        text = re.sub(r'\\\r?\n', '', text)
        tokens = []
        directive = False
        line_start = True
        for match in C_TOKENS.finditer(text):
            token = match.group()
            if token.startswith(('/*', '//')):
                if '\n' in token:
                    if directive:
                        tokens.append('\n')
                    directive = False
                    line_start = True
                continue
            if token.isspace():
                if '\n' in token:
                    if directive:
                        tokens.append('\n')
                    directive = False
                    line_start = True
                continue
            if token == '#' and line_start:
                directive = True
            tokens.append(token)
            line_start = False
        return tokens
    if suffix == '.py':
        ignored = {tokenize.COMMENT, tokenize.NL, tokenize.ENCODING}
        return [(item.type, item.string) for item in
                tokenize.generate_tokens(StringIO(text).readline)
                if item.type not in ignored]
    if suffix == '.cmake' or Path(path).name == 'CMakeLists.txt':
        # Quoted and bracket arguments retain their exact contents. Hashes in
        # either are values, not comments; bracket comments can span lines.
        tokens = []
        cursor = 0
        while cursor < len(text):
            bracket = re.match(r'(#?)\[(=*)\[', text[cursor:])
            if bracket:
                end_mark = ']' + bracket.group(2) + ']'
                end = text.find(end_mark, cursor + bracket.end())
                if end < 0:
                    return content
                end += len(end_mark)
                if not bracket.group(1):
                    tokens.append(text[cursor:end])
                cursor = end
            elif text[cursor] == '"':
                quoted = re.match(r'"(?:\\.|[^"\\])*"', text[cursor:], re.DOTALL)
                if not quoted:
                    return content
                tokens.append(quoted.group())
                cursor += quoted.end()
            elif text[cursor] == '#':
                newline = text.find('\n', cursor)
                cursor = len(text) if newline < 0 else newline + 1
            elif text[cursor].isspace():
                cursor += 1
            elif text[cursor] == '\\':
                return content
            else:
                atom = re.match(r'[^\s()#"\[\]\\]+|.', text[cursor:])
                tokens.append(atom.group())
                cursor += atom.end()
        return tokens
    return content


def git(directory, *arguments):
    return subprocess.check_output(['git', '-C', str(directory), *arguments],
                                   stderr=subprocess.DEVNULL)


def blob(directory, revision, path):
    try:
        return git(directory, 'show', f'{revision}:{path}')
    except subprocess.CalledProcessError:
        return None


def changed(directory, base, head, inspect_common=True):
    paths = git(directory, 'diff', '--name-only', '-z', base, head).split(b'\0')
    for encoded in paths:
        if not encoded:
            continue
        path = encoded.decode('utf-8')
        if not relevant(path):
            continue
        if path == 'Common' and inspect_common:
            old = git(directory, 'rev-parse', f'{base}:Common').decode().strip()
            new = git(directory, 'rev-parse', f'{head}:Common').decode().strip()
            if changed(directory / 'Common', old, new, False):
                return True
            continue
        before, after = blob(directory, base, path), blob(directory, head, path)
        if before is None or after is None:
            return True
        if source_tokens(path, before) != source_tokens(path, after):
            return True
    return False


def event_revisions(event, name):
    if name == 'push':
        return event.get('before'), event.get('after')
    if name == 'pull_request':
        return event['pull_request']['base']['sha'], os.environ.get('GITHUB_SHA')
    return None, None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--base')
    parser.add_argument('--head', default='HEAD')
    options = parser.parse_args()
    base, head = options.base, options.head
    if not base and os.environ.get('GITHUB_EVENT_PATH'):
        event = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text())
        base, head = event_revisions(event, os.environ.get('GITHUB_EVENT_NAME'))
    build = True
    try:
        if base and head and re.fullmatch(r'[0-9a-fA-F]{40}', base) and int(base, 16):
            build = changed(Path.cwd(), base, head)
    except (subprocess.CalledProcessError, UnicodeError, tokenize.TokenError,
            IndentationError, OSError):
        # Missing history or uncertain syntax must never suppress a needed build.
        build = True
    result = f'build={str(build).lower()}\n'
    sys.stdout.write(result)
    if os.environ.get('GITHUB_OUTPUT'):
        with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf-8') as output:
            output.write(result)


if __name__ == '__main__':
    main()
