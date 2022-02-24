import shutil
import os
import socket
import time 
import numpy as np
import cv2
from trt_detector import TRTDetector
from network import buff2numpy
from inference import draw_dets, write_dets
import threading, queue
import sys, os, shutil
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
        #self.sock.settimeout(1)
        self.sock.bind((self.TCP_IP, self.TCP_PORT))
        
        self.clients_connected=0
        self.max_clients=100

        self.detection_queue = q = queue.Queue()

    @property
    def randbytes(self):
        return open("/dev/random","rb").read(4)

    def get_ip(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            # doesn't even have to be reachable
            s.connect(('10.255.255.255', 1))
            IP = s.getsockname()[0]
        except Exception:
            IP = '127.0.0.1'
        finally:
            s.close()
        return IP

    def client_manager(self):
        self.sock.listen(5)
        id=0
        while(True):
            #Block waiting for client
            client, addr = self.sock.accept() 
            addr = addr[0] #Use just the ip address

            print("[Client Manager] Connected to client %s"%addr)

            #Make a directory for client output
            os.makedirs("/root/gap_runner/web/htdocs/%s/"%addr,exist_ok=True)

            #Copy index file to directory
            shutil.copyfile("/root/gap_runner/web/htdocs/index_template.html", "/root/gap_runner/web/htdocs/%s/index.html"%addr)

            #Hand client off to data collecting thread
            client_thread = threading.Thread(target=self.data_collector, args=(client, addr,id)) 
            client_thread.start()
            self.clients_connected = self.clients_connected + 1
            id=id+1

    def data_collector(self, client, addr, id):
        
        print("[Data Collector %d] Connected to client at %s" % (id, addr)) 
        self.clients_connected = 1
        client.send(self.randbytes)
        counter = 0
        total_size = 0
        data = b''
        start = False
        count = 0
        new_packet = False

        while True:
            content = client.recv(self.buffer_size)

            #print("[Data Collector %d] got %d bytes"%(id,len(content)))

            if len(content) == 0:
                #print("[Data Collector %d] got zero length packet"%id)
                continue
                
            if int(content[1]) == 0:
                new_packet = True
                #print("[Data Collector %d] got new packet"%id)

            if new_packet:
                client.send(self.randbytes)
                new_packet = False

            if int(content[1]) != 0 and not start:
                #print("[Data Collector %d] content[1] != 0 and not start"%id)
                continue
            elif int(content[1]) == 0:
                #print("[Data Collector %d] got int(content[1]) == 0"%id)
                start = True

            packetData = content[4:]
            real_length = int(content[2]) << 8 | int(content[3])
            total_size += real_length
            data = data + packetData[:real_length]
            
            #print("[Data Collector %d] real length: %d"%(id,real_length))
            #print("[Data Collector %d] have %d / %d bytes"%(id, total_size, self.target_bytes))
                
            #Have whole data instance, pass to decoder
            if total_size == self.target_bytes:
                item = [data, addr, count]
                self.detection_queue.put(item)
                print("[Data Collector %d] Sent instance %d for processing" % (id, count)) 

                #Reset accumulation
                count += 1
                data = b''
                start = False
                total_size = 0

    def detection_server(self): 

        print("[Detector] Detector waiting for data")
        count  =0 
        while(True):

            #Block waiting for frame from client
            data, addr, client_count = self.detection_queue.get()

            print("[Dectector] Got frame %d from client %s"%(client_count,addr))

            start_time = time.time()

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
            time_diff = time.time() - start_time
            
            time_stamp = int(time.time()*1000.0)

            fname = "/root/gap_runner/web/htdocs/%s/frame.jpg"%addr
            fname2 = "/root/gap_runner/web/htdocs/%s/frame-%d.jpg"%(addr,time_stamp)
            cv2.imwrite(fname, img)
            shutil.copyfile(fname,fname2)

            for i in range(8):
                h = channels[0, i] # H W
                h = h / 6 #h.max() #note that h \in [0, 6] from ReLU6
                h = h * 255
                h = h.astype(np.uint8)
                h = cv2.applyColorMap(h, cv2.COLORMAP_JET)
                fname = "/root/gap_runner/web/htdocs/%s/h%d.png"%(addr, i+1)
                fname2 = "/root/gap_runner/web/htdocs/%s/h%d-%d.png"%(addr,i+1,time_stamp)
                cv2.imwrite(fname, h)
                shutil.copyfile(fname,fname2)
            
            fname = "/root/gap_runner/web/htdocs/%s/dets.json"%addr
            fname2 = "/root/gap_runner/web/htdocs/%s/dets-%d.json"%(addr,time_stamp)
            write_dets(dets, fname, count) #write to json
            shutil.copyfile(fname,fname2)

            count += 1
            print("[Dectector] Processed frame %d from client %s in %d ms"%(client_count,addr,time_diff*1000))
    
    def server_advertiser(self,advertise_port = 2222, server_port = 8584):

        server_ip = self.get_ip()

        in_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)  # UDP
        in_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        in_socket.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        in_socket.bind(("", advertise_port))
        #in_socket.settimeout(1)

        out_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        out_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        out_socket.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        out_socket.settimeout(0.1)

        print("[Server Advertiser] Advertising CLIO server on UDP:", server_ip)


        while(True):
            #Wait for udp request for server identity
            data, addr = in_socket.recvfrom(advertise_port)

            data_str = data.decode("ascii")

            #Respond only if request is for CLIO server and currenrly
            #serving less than max_clients
            if(data_str=="get-clio-server" and self.clients_connected < self.max_clients):
                print("[Server Advertiser] Received request from %s " % (addr[0]))
                message = "set-clio-server " + server_ip + " %d"%server_port
                out_socket.sendto(bytes(message,"ascii"), (addr[0], advertise_port))
                print("[Server Advertiser] Replied with: " + message) 

    def run(self, client, addr, id):
        print("Detection server waiting for TCP conntection...")
        self.sock.listen(5)

        #Block waiting for client
        client, addr = self.sock.accept() 
    
        print("Thread %d connected to client at %s" % (id, addr[0])) 
        self.clients_connected = 1
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
    use_xavier=True

    print("Booting up nano detection server")
    trt_file = "/root/gap_runner/nano/"

    if from_img:
        trt_file = trt_file + "full"
    else:
        trt_file = trt_file + "suffix"

    if(use_xavier): 
        trt_file = trt_file + "_xavier.trt"
    else:
        trt_file = trt_file + ".trt"        

    detector = TRTDetector(trt_file)
    print("TRT modelfile %s opened"%trt_file)
    
    #detector=[]
    server = NanoServer(detector, from_img=from_img)
    
    #Start server advertising thread
    adv_thread = threading.Thread(target=NanoServer.server_advertiser, args=(server,)) 
    adv_thread.start()

    #Start thread to accept multiple clients
    client_manager_thread = threading.Thread(target=NanoServer.client_manager, args=(server,)) 
    client_manager_thread.start()   

    #Start running decoder in main thread
    server.detection_server()

    #Start waiting for TCP client to connect
    #server.run()
