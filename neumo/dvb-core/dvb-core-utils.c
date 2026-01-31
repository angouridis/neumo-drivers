#include <linux/usb.h>
#include <linux/siphash.h>
#include <media/neumo-dvb-frontend.h>
//some arbitrary seed
static const siphash_key_t my_key = {{  0x0102030405060708,
																					0x090a0b0c0d0e0f10}};

u64 usb_serial_to_mac_address(const char *serial)
{
	if (!serial)
		return 0;

	u64 ret = siphash(serial, strlen(serial), &my_key);
	//we make the number positive and leave some bits at the end to allow multiple adapters
	return ret & 0x7fffffffffffff00;
}
EXPORT_SYMBOL_GPL(usb_serial_to_mac_address);
