import importlib.util
from pathlib import Path
import unittest
import sys

sys.dont_write_bytecode = True

spec = importlib.util.spec_from_file_location(
    'build_changes', Path(__file__).resolve().parents[2] / 'tools/build_changes.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class BuildChangesTest(unittest.TestCase):
    def same(self, path, left, right):
        self.assertEqual(module.source_tokens(path, left.encode()),
                         module.source_tokens(path, right.encode()))

    def different(self, path, left, right):
        self.assertNotEqual(module.source_tokens(path, left.encode()),
                            module.source_tokens(path, right.encode()))

    def test_comments(self):
        self.same('a.c', 'int x; /* before */\n', 'int x; /* after */\n// new\n')
        self.same('a.h', '// before\n#define X 3\n', '// after\n#define X 3\n')
        self.same('CMakeLists.txt', '# old\nset(X "#value")', '# new\nset(X "#value")')
        self.same('a.cmake', '#[=[old\ncomment]=]\nset(X 1)', 'set(X 1)')
        self.same('a.py', 'value = 1 # before\n', 'value = 1 # after\n')

    def test_values_and_tokens(self):
        self.different('a.c', 'puts("// before");', 'puts("// after");')
        self.different('a.c', 'int/**/value;', 'intvalue;')
        self.different('a.c', 'x++;', 'x + + y;')
        self.different('a.c', '1e+5;', '1e +5;')
        self.different('a.c', 'L"text";', 'L "text";')
        self.different('a.cmake', 'set(X ab)', 'set(X a b)')
        self.different('a.h', '#define X a\nb\n', '#define X a b\n')
        self.different('a.cmake', 'set(X "# old")', 'set(X "# new")')
        self.different('a.cmake', 'set(X [=[# old]=])', 'set(X [=[# new]=])')
        self.different('a.py', 'value = "#old"\n', 'value = "#new"\n')

    def test_documentation(self):
        for path in ('README.md', 'guide.MD', 'docs/example.json', 'notes.txt'):
            self.assertFalse(module.relevant(path))
        for path in ('CMakeLists.txt', 'cmake/build.cmake', 'Common', 'src/main.c'):
            self.assertTrue(module.relevant(path))


if __name__ == '__main__':
    unittest.main()
