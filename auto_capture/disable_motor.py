import ebb_motion
import ebb_serial

port = ebb_serial.openPort()
ebb_motion.sendDisableMotors(port)