#sudo service docker restart

#echo "Starting Apache web server in docker on port 8080"
#sudo docker run --rm -d \
    #-p 8080:80 \
    #-v "$PWD/htdocs":/usr/local/apache2/htdocs/ \
    #httpd:2.4-alpine

#hostname=`hostname`

#echo "Starting image server. Go to $hostname:8080 to view"
#python3 img_server.py

#nano_server:latest \
#--runtime nvidia \
sudo docker run -it\
    --network host \
    --privileged \
    --gpus all \
    -v $PWD:/root/gap_runner \
    colinski/python \
    python -u /root/gap_runner/nano/track.py /root/gap_runner/nano/suffix.onnx
