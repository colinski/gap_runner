sudo docker run -it --rm \
    --network host \
    --privileged \
    -v $PWD:/root/gap_runner \
    gap_sdk \
    python -u /root/gap_runner/gap8_server.py
