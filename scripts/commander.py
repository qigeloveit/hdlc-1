import serial
import imp




hdlc_escape = 0x7d
hdlc_flag = 0x7e
hdlc_address = 0xff
hdlc_control = 0x03
cmd_echo = 1
cmd_calibrate = 2
cmd_timeout = 3
status_ok = 0
status_invalid_response = 1
status_crc_at_master = 1
status_crc_at_slave = 1<<4

def from_little_endian(l):
    r = 0
    for d in l:
        assert int(d) < 256
        r = r << 8 
        r = r + int(d)

    return d

class HdlcException(Exception):
    def __init__(self,s):
        self.s = s
        
    def __str__(self):
        return str(self.s)

class HdlcFrameError(HdlcException):
    pass

class HdlcCrcError(HdlcException):
    pass

class HdlcSlaveError(HdlcException):
    pass

class Temperature:
    #temperature in milli Kelvin
    def __init__(self,v):
        self.v = v

    def __str__(self):
        return self.v/1000 + "." (self.v % 1000)/100 + " K (" + (self.v-271150)/1000 + "." ((self.v-271150) % 1000)/100 + "C )"


class Status:

    def __init__(self,d):
        assert len(d) > 2*5 

        self.timeout = from_little_endian(d[0:1])
        self.currentTime = from_little_endian(d[2:3])
        self.targetTemperature = Temperature(from_little_endian(d[4:5]))
        self.currentTemperature = Temperature(from_little_endian(d[6:7]))
        self.position = from_little_endian(d[8:9])
        
    def __str__(self):
        s = ""
        s += "Timeout: " + self.timeout + " s\n"
        if self.currentTime == 0:
            s += "Timer: stopped\n"
        else:
            s += "Timer: running, " + self.currentTime + " s from timeout\n"

        s += "Temperature: " + self.currentTemperature.str() + "\n"

        s += "Brewing temperature: " + self.targetTemperature.str() + "\n"

        s += "Position: " + self.position + " mm\n"

class HdlcFrame(list):
    def __init__(self, payload=[],address=hdlc_address,control=hdlc_control):
        list.__init__(self,payload)
        self.address=address
        self.control=control

    def __str__(self):
        return self.format()
        
    def format(self,base=16):
        formated_frame = ""
        formated_frame += "addr:{0:02x} ".format(self.address)
        formated_frame += "ctrl:{0:02x} ".format(self.control)       
        formated_frame += "data:"
        for i in self:
            formated_frame += "{0:02x} ".format(i)


        return formated_frame

    def encode(self):
        frame = [hdlc_flag,self.address,self.control]
        for d in self:
            if hdlc_escape == d or hdlc_flag == d:
                frame.append(hdlc_escape)
                frame.append(d ^ 0x20)
            else:
                frame.append(d)

    #append dummy CRC and EOF marker
        frame += [0x00,0x00,hdlc_flag]
        return frame

    @classmethod
    def decode(cls,frame):
    #assume that frame starts and ends with flag
        data = list()
        i = 0

    #forward to start sequence
        while i<len(frame) and frame[i] !=  hdlc_flag:
            i+=1

    #remove start sequence
        i+=1

    #transform escaped bytes
        while i<len(frame) and frame[i] !=  hdlc_flag:
            if frame[i] == hdlc_escape and i+1 < len(frame):
                data.append(frame[i+1] ^ 0x20)
                i+=1
            else: 
                data.append(frame[i])
           
            i+=1

        if len(data) < 4:
            raise HdlcFrameError(len(data))

    #dummy CRC check
        if data[-2] != 0 or data[-1] != 0:
            raise HdlcCrcError(0)

        if status_ok != data[1]:
            raise HdlcSlaveError(data[1])
            
        cls(data[2:-2],data[0],data[1])
    


def transmit_payload(port,data,address=hdlc_address,control=hdlc_control): 
    
    command = HdlcFrame(data,address,control)
    
    print ("<- " + command + "( " + command.encode() + ")")

    port.write(bytearray(command.encode()))


def read_frame(port,timeout=None):
    port.timeout = timeout
    data = port.read(512)
    l = list(data)
    return l

        

def open_port(port="/dev/ttyACM1"):
    return serial.Serial(port,baudrate=115200,timeout=3)

def read_port(port,timeout=None):
    port.timeout=timeout
    data = port.read()
    print (data)

def exchange(port,data,address=hdlc_address,control=hdlc_control,timeout=None,retry=0):

    trial=0
    while True:

        transmit_payload(port,data,address,control)
        raw = read_frame(port,timeout)
        try:
            frame = HdlcFrame.decode(raw)
        except HdlcException:
            trial+=1
            print ("exch " + trial + " -> " + raw + " invalid")
            if trial == retry:
                print("give up")
                return None
        else:
            print ("-> ( " + raw + " ) " + frame)            
            return frame


def start_timeout(port,time=0,timeout=None,trials=10):
    print("Set timeout to " + time)
    request = [time & 0xFF, time >> 8]
    response = exchange(port,request,cmd_timeout,0,timeout,trials)
    if response != None:
        print("Timeout started")
    else:
        print("Starting timeout failed")
    

def calibrate(port,temperature,timeout=None,trials=10):
    request = [temperature & 0xFF, temperatue >> 8]
    response = exchange(port,request,cmd_calibrate,0,timeout,trials)
    return check_response(response)

def echo(port,payload,timeout=None,trials=10):
    response = exchange(port,payload,cmd_echo,0,timeout,trials)
    status = check_response(response)
    if  status_ok != status:
        return status
    else:
        return get_payload(response)


def get_status(port,timeout=None,trials=10):
    response = exchange(port,[],cmd_status,0,timeout,trials)
    if status_ok == check_response(response):
        return Status(get_payload(response))
