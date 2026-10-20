#!/usr/bin/env python3
"""プラグイン本体・CMake・Claude Code のプラグイン定義のバージョンがそろっているかを確かめる。"""
import json, os, re, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')


def read(path):
    with open(os.path.join(ROOT, path), encoding='utf-8') as f:
        return f.read()


versions = {
    'core/engine.h kVersion': re.search(r'kVersion = "([^"]+)"', read('core/engine.h')).group(1),
    'CMakeLists.txt project(VERSION)': re.search(r'project\(\w+ VERSION ([0-9.]+)', read('CMakeLists.txt')).group(1),
    'plugin/.claude-plugin/plugin.json version': json.loads(read('plugin/.claude-plugin/plugin.json')).get('version', '(なし)'),
}
for k, v in versions.items():
    print(f'{v:10s} {k}')
if len(set(versions.values())) != 1:
    sys.exit('バージョンがそろっていません')
