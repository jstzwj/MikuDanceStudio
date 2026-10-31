"""CPU dependency gates reject GPU/UI libraries, independent of loader addresses."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from audit_model_runtime import check_binary_dependencies


class DependencyAudit(unittest.TestCase):
    def test_linux_loader_metadata_is_not_a_dependency(self):
        check_binary_dependencies([
            '\tlibm.so.6 => /lib/x86_64-linux-gnu/libm.so.6 (0x00007f9fd3d19000)',
            '\tlinux-vdso.so.1 (0x00007fd3d9000000)',
            '\t/lib64/ld-linux-x86-64.so.2 (0x00007fd3d0000000)',
            'libc.so.6 => /opt/vulkan-build/libc.so.6 (0x00007fabc0000000)',
        ], 'linux')

    def test_actual_library_names_remain_forbidden(self):
        cases = [
            ('win32', 'D3DX9_43.dll'),
            ('win32', 'USER32.dll'),
            ('linux', 'libvulkan.so.1 => /usr/lib/libvulkan.so.1 (0x00007fabc0000000)'),
            ('linux', 'libQt6Core.so.6 => /usr/lib/libQt6Core.so.6 (0x00007fabc0000000)'),
            ('linux', 'libhelper.so => /opt/libd3d9.so (0x00007fabc0000000)'),
            ('darwin', '@rpath/libMoltenVK.dylib (compatibility version 1.0.0, current version 1.4.2)'),
            ('darwin', '/opt/QtCore.framework/Versions/A/QtCore (compatibility version 6.0.0, current version 6.8.0)'),
        ]
        for platform, library in cases:
            with self.subTest(platform=platform, library=library):
                with self.assertRaisesRegex(RuntimeError, 'forbidden binary dependency'):
                    check_binary_dependencies([library], platform)

    def test_missing_dependency_is_a_failure(self):
        with self.assertRaisesRegex(RuntimeError, 'unresolved binary dependency'):
            check_binary_dependencies(['libordinary.so => not found'], 'linux')

    def test_native_runtime_dependencies(self):
        check_binary_dependencies(['KERNEL32.dll', 'MSVCP140.dll', 'VCRUNTIME140.dll'], 'win32')
        check_binary_dependencies([
            '/usr/lib/libc++.1.dylib (compatibility version 1.0.0, current version 1900.0.0)',
            '/usr/lib/libSystem.B.dylib (compatibility version 1.0.0, current version 1351.0.0)',
        ], 'darwin')


if __name__ == '__main__':
    unittest.main()
