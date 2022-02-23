import shutil
import os
import socket
import time 
import numpy as np
import cv2
from trt_detector import TRTDetector
from network import buff2numpy
from inference import draw_dets, write_dets
from datetime import datetime

class NanoServer:
    def __init__(self, detector,
                 TCP_IP='0.0.0.0', TCP_PORT=8584,
                 buffer_size=730, from_img=False
    ):
        self.detector = detector
        self.TCP_IP = TCP_IP
        self.TCP_PORT = TCP_PORT
        self.buffer_size = buffer_size
        self.from_img = from_img
        if self.from_img:
            self.target_bytes = 224*224
        else: 
            self.target_bytes = 8*28*28 + 224*224
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((self.TCP_IP, self.TCP_PORT))

    @property
    def randbytes(self):
        return open("/dev/random","rb").read(4)

    def run(self):
        print("detection server waiting for TCP conntection...")
        self.sock.listen(5)
        client, addr = self.sock.accept()
        print("connected to client at %s" % addr[0]) 
        client.send(self.randbytes)
        counter = 0
        total_size = 0
        data = b''
        start = False
        count = 0
        new_packet = False

        while True:
            start_time = time.time()
            content = client.recv(self.buffer_size)
            if len(content) == 0:
                continue
            if int(content[1]) == 0:
                new_packet = True
            if new_packet:
                client.send(self.randbytes)
                new_packet = False
            if int(content[1]) != 0 and not start:
                continue
            elif int(content[1]) == 0:
                start = True
            packetData = content[4:]
            real_length = int(content[2]) << 8 | int(content[3])
            total_size += real_length
            data = data + packetData[:real_length]
            
            if total_size == self.target_bytes:
                num_pixels = 224*224
                img = buff2numpy(data[0:num_pixels], dtype=np.uint8)
                img = img.reshape(224, 224)
                if self.from_img:
                    model_input = img.reshape(1, 1, 224, 224)
                    model_input = model_input.astype(np.float32)
                    model_input = (model_input - 114.0) / 57.0
                    dets = self.detector.detect(model_input)
                    channels = np.ones((1, 8, 28, 28))
                else:
                    channels = buff2numpy(data[num_pixels:], dtype=np.int8)
                    channels = channels.reshape(8, 28, 28)
                    channels = channels.astype(np.float32)
                    channels = (channels - -128) * 0.02352941
                    z_channels = np.zeros([32-8, 28, 28], dtype=np.float32)
                    channels = np.concatenate([channels, z_channels], axis=0)
                    channels = np.expand_dims(channels, axis=0) #1 C H W
                    dets = self.detector.detect(channels)
                
                img = np.stack([img] * 3, axis=-1) #convert to "color"
                img = draw_dets(img, dets)
               
                root = '/root/gap_runner/web/htdocs/'
                timestamp = str(datetime.now())
                timestamp = timestamp.split('.')[0]
                timestamp = '-'.join(timestamp.split())
                
                fname = '%s/history/%s-img.jpg' % (root, timestamp)
                ln_fname = '%s/imgs/frame.jpg' % root
                cv2.imwrite(fname, img)
                cv2.imwrite(ln_fname, img)
                # os.system('ln -sf %s %s' % (img_fname, ln_fname))
                # cv2.imwrite('%s/imgs/frame.jpg' % root, img)
                # os.symlink(img_fname, '%s/imgs/img.jpg' % root)
                # shutil.copy(img_fname, '%s/imgs/frame.jpg' % root)
                for i in range(8):
                    h = channels[0, i] # H W
                    h = h / h.max() #note that h \in [0, 6] from ReLU6
                    h = h * 255
                    h = h.astype(np.uint8)
                    h = cv2.applyColorMap(h, cv2.COLORMAP_JET)
                    
                    fname = '%s/history/%s-h%d.png' % (root, timestamp, i+1)
                    ln_fname = '%s/imgs/h%d.png' % (root, i+1)
                    cv2.imwrite(fname, h)
                    cv2.imwrite(ln_fname, h)
                    # os.system('ln -sf %s %s' % (fname, ln_fname))
                    #shutil.copy(fname, '%s/imgs/h%d.png' % (root, i+1))
                    # os.symlink(fname, '/root/gap_runner/tmp.link')
                    # os.rename('/root/gap_runner/tmp.link', ln_fname)
                
                # fname = '/root/gap_runner/web/htdocs/imgs/dets.json'
                fname = '%s/history/%s-dets.json' % (root, timestamp)
                write_dets(dets, fname, count) #write to json

                count += 1
                data = b''
                start = False
                total_size = 0
                time_diff = time.time() - start_time
                print('processed detection %d in %d ms' % (count, time_diff*1000))
               
if __name__ == '__main__':
    from_img = False
    print("booting up nano detection server")
    if from_img:
        detector = TRTDetector('/root/gap_runner/nano/full.trt')
    else:
        detector = TRTDetector('/root/gap_runner/nano/suffix.trt')
    print("TRT detector suffix opened")
    server = NanoServer(detector, from_img=from_img)
    while True:
        server.run()