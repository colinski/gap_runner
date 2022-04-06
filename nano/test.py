import onnxruntime as ort
import numpy as np
from tqdm import tqdm, trange
import time


#model = '/root/gap_runner/nano/suffix.onnx'
#img = np.zeros((1, 32, 28, 28), dtype=np.float32)

model = '/root/gap_runner/gap/prefix.onnx'
img = np.zeros((1, 1, 224, 224), dtype=np.float32)

#sess = ort.InferenceSession(model, providers=['CPUExecutionProvider'])
sess = ort.InferenceSession(model, providers=['CUDAExecutionProvider'])
# sess = ort.InferenceSession(model, providers=['TensorrtExecutionProvider'])


input_name = sess.get_inputs()[0].name

for i in range(1000):
    start = time.time()
    outputs = sess.run([], {input_name: img})[0]
    esp_time = time.time() - start
    print(i, esp_time*1000)
