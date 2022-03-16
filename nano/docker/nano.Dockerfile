FROM nvcr.io/nvidia/l4t-ml:r32.5.0-py3
SHELL ["/bin/bash", "-c"] 
MAINTAINER csamplawski@cs.umass.edu

RUN pip3 install websockets
RUN pip3 install filterpy #Kalman filter
RUN pip3 install lap #Linear Assignment Problem (solver)
