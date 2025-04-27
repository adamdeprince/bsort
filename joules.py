import sys
import yoctopuce
from yoctopuce.yocto_api import *
from yoctopuce.yocto_power  import *
errmsg = YRefParam()
try:
    YAPI.RegisterHub("usb", errmsg)
    sensor = YPower.FirstPower()
    # power = sensor.get_curentValue()
    # sensor.reset()
    
    print(sensor.get_deliveredEnergyMeter() * 1000.0)
finally:
    YAPI.FreeAPI()
