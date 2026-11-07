# Send stock V15 into the chip's ROM boot (WL82 UBOOT1.00) with MvaveFM1Unbricker's USB-MIDI key. It writes
# nothing, and a power cycle brings the unit back. Start fm1_rescue.py first, so it is waiting for the unit.
import os, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.realpath(__file__)))))
sys.path.insert(0, os.path.join(ROOT, "firmware", "fm1-base", "tools"))
import fm1_install as fi

KEY = bytes([0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7])

dev = fi.Updater(fi.MidoBackend()).find()
if dev is None:
    sys.exit("no FM-1 answered: not sending the key")
print(f"port: {dev.name}  identity: {dev.id.text}")
if "compatible" in dev.name.lower() or not fi.is_stock(dev.id.text):
    sys.exit("not stock M-VAVE firmware on the hardware port: not sending the key")
for n in range(3):
    if not dev.link.send(KEY):
        print("send stopped (unit may have left the bus)")
        break
    time.sleep(0.02)
time.sleep(0.5)
print("key sent 3 times")
