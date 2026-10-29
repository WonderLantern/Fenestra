"""Select a native host transport; imports never access a device."""
import os
import sys
from ptp_contract import check_profile


def open_native_transport(profile='auto'):
    check_profile(profile)
    if sys.platform=='win32':
        from ptp_wpd import WpdPtp
        return WpdPtp(profile=profile)
    if sys.platform.startswith('linux'):
        from ptp_usb import UsbPtp
        return UsbPtp(profile=profile)
    raise RuntimeError('Fenestra PTP hosts supported: Windows WPD or Linux PyUSB/libusb')


def open_transport():
    profile=check_profile(os.environ.get('FENESTRA_PTP_PROFILE','auto'))
    config=os.environ.get('FENESTRA_PTP_PROCESS_CONFIG')
    if config:
        from ptp_process import ProcessPtp
        return ProcessPtp.from_config(config,profile)
    return open_native_transport(profile)
