"""Checks for the reproducible shared-Core compile boundary."""
import sys
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'host'))
import build_zf_core


class ZfCoreCompileTests(unittest.TestCase):
    def test_owner_worker_cannot_bypass_experimental_adapter_gate(self):
        with self.assertRaisesRegex(ValueError, 'requires experimental adapters'):
            build_zf_core.build(None, None, experimental_adapters=False, owner_worker=True)
        with self.assertRaisesRegex(ValueError, 'requires experimental adapters'):
            build_zf_core.build(None, None, experimental_adapters=False, native_menu=True)

    def test_source_inventory_is_nonempty_and_target_specific_link_is_absent(self):
        self.assertEqual(len(build_zf_core.SHARED_SOURCES), 66)
        self.assertIn('src/native_scene.c', build_zf_core.SHARED_SOURCES)
        self.assertIn('src/native_display.c', build_zf_core.SHARED_SOURCES)
        self.assertIn('src/platform_native.c', build_zf_core.SHARED_SOURCES)
        self.assertIn('src/ptp.c', build_zf_core.SHARED_SOURCES)
        self.assertIn('src/ptp_device_info.c', build_zf_core.SHARED_SOURCES)
        self.assertIn('src/native_call_a64.S', build_zf_core.SHARED_SOURCES)
        self.assertNotIn('src/platform/z9_c532/menu.c', build_zf_core.SHARED_SOURCES)

    def test_experimental_adapter_inventory_is_guarded(self):
        names = {name for name, _ in build_zf_core.EXPERIMENTAL_SOURCES}
        self.assertEqual(names, {
            'src/platform/zf_c301/menu_setup.c',
            'src/platform/zf_c301/sysctl_gate.c',
            'src/platform/zf_c301/lifecycle.c',
            'src/platform/zf_c301/owner_storage.c',
            'src/platform/zf_c301/owner_publish.c',
            'src/platform/zf_c301/owner_native.c',
            'src/platform/zf_c301/owner_task.c',
            'src/platform/zf_c301/owner_fault.c',
            'src/platform/zf_c301/rtos_profile.c',
            'src/platform/zf_c301/linux_profile.c',
            'src/platform/zf_c301/apps_flush.c',
            'src/platform/zf_c301/loaded_modules.c',
            'src/platform/zf_c301/apps_domain.c',
            'src/platform/zf_c301/task.c',
            'src/platform/zf_c301/ptp.c',
            'src/platform/zf_c301/ptp_admission.c',
            'src/platform/zf_c301/display_profile.c',
            'src/platform/zf_c301/menu_flush.c',
            'src/platform/zf_c301/services.c',
            'src/platform/zf_c301/card.c',
            'src/platform/zf_c301/module_stage.c',
            'src/platform/zf_c301/module_runtime.c',
            'src/platform/zf_c301/exec_context.c',
            'src/platform/zf_c301/address_space.c',
            'src/platform/zf_c301/private_call.S',
            'src/platform/zf_c301/user_call.S',
            'src/platform/zf_c301/card_native.c',
            'src/platform/zf_c301/ui_dispatch_candidates.c',
            'src/platform/zf_c301/ui_dispatch_profile.c',
            'src/platform/zf_c301/startup_trampoline.S',
            'src/platform/zf_c301/ptp_trampoline.S'})

    def test_link_audit_classifies_unresolved_symbol_ownership(self):
        self.assertEqual(build_zf_core._classify_unresolved_symbol('__aarch64_ldadd4_acq'), 'compiler-runtime')
        self.assertEqual(build_zf_core._classify_unresolved_symbol('fx_zf_after_startup_complete'), 'zf-lifecycle')
        self.assertEqual(build_zf_core._classify_unresolved_symbol('fx_native_ui_dispatch_profile'), 'zf-ui-dispatch-boundary')
        self.assertEqual(build_zf_core._classify_unresolved_symbol('fx_ptp_disconnect'), 'zf-ptp-boundary')

    def test_compiler_result_is_explicitly_not_a_runtime_image(self):
        # This is a source-level invariant; no cross compiler or firmware is
        # needed to verify that the receipt cannot be mistaken for a package.
        self.assertTrue(build_zf_core.__doc__)
        self.assertIn('without producing a target image', build_zf_core.__doc__)

    def test_zf_native_menu_has_platform_target_and_signed_text_id_bounds(self):
        source = (ROOT / 'src/platform/zf_c301/menu_native.c').read_text()
        self.assertIn('#include "fenestra_platform_native.h"', source)
        self.assertIn('id>=(int32_t)FX_ZF_MANAGER_TEXT_BASE', source)
        self.assertIn('id<(int32_t)FX_ZF_MODULE_TEXT_BASE', source)

    def test_zf_module_first_open_hides_reserved_rows_and_reflows(self):
        apps = (ROOT / 'src/platform/zf_c301/menu_apps.c').read_text()
        flush = (ROOT / 'src/platform/zf_c301/menu_flush.c').read_text()
        self.assertIn('fx_zf_menu_opened_native((FxNativePage *)p)', apps)
        self.assertIn('fx_native_row_visibility(&ui_visibility,page,row,visible)', flush)
        self.assertIn('fx_native_row_reflow(&ui_layout,page)', flush)


if __name__ == '__main__':
    unittest.main()
