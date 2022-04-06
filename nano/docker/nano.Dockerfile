FROM nvcr.io/nvidia/l4t-ml:r32.5.0-py3
SHELL ["/bin/bash", "-c"] 
MAINTAINER csamplawski@cs.umass.edu

RUN pip3 install websockets
RUN pip3 install filterpy #Kalman filter
RUN pip3 install lap #Linear Assignment Problem (solver)

RUN wget https://nvidia.box.com/shared/static/jy7nqva7l88mq9i8bw3g3sklzf4kccn2.whl -O onnxruntime_gpu-1.10.0-cp36-cp36m-linux_aarch64.whl
RUN pip3 install onnxruntime_gpu-1.10.0-cp36-cp36m-linux_aarch64.whl
RUN pip3 install tqdm
