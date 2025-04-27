import yoctopuce
from yoctopuce.yocto_api import *
from yoctopuce.yocto_power  import *
errmsg = YRefParam()
try:
    YAPI.RegisterHub("usb", errmsg)
    sensor = YPower.FirstPower()
    if not sensor:
        print(errmsg)
        exit(1)
    # power = sensor.get_curentValue()
    sensor.reset()
    #sensor.get_deliveredEnergyMeter()
finally:
    YAPI.FreeAPI()
