import numpy as np
from filterpy.kalman import KalmanFilter

#xyar = x,y position of center, area, aspect ratio
#xyxy = default format for mmdet and cv2 rectange
def xyxy_to_xyar(bbox):
    w = bbox[2] - bbox[0]
    h = bbox[3] - bbox[1]
    x = bbox[0] + w / 2
    y = bbox[1] + h / 2
    a = w * h
    r = w / h
    return np.array([x, y, a, r])

def xyar_to_xyxy(bbox):
    w = np.sqrt(bbox[2] * bbox[3])
    h = bbox[2] / w
    x1 = bbox[0] - w / 2
    y1 = bbox[1] - h / 2
    x2 = bbox[0] + w / 2
    y2 = bbox[1] + h / 2
    return np.array([x1, y1, x2, y2])

class KalmanTrack:
    count = 0 #global count across tracks for id
    def __init__(self, bbox):
        self.kf = KalmanFilter(dim_x=7, dim_z=4)
        self._init_kf(bbox)
        self.time_since_update = 0
        self.id = KalmanTrack.count
        KalmanTrack.count += 1
        self.hit_streak = 0
        self.age = 0
    
    #define constant velocity model
    def _init_kf(self, bbox):
        self.kf.F = np.array([[1,0,0,0,1,0,0],
                              [0,1,0,0,0,1,0],
                              [0,0,1,0,0,0,1],
                              [0,0,0,1,0,0,0],  
                              [0,0,0,0,1,0,0],
                              [0,0,0,0,0,1,0],
                              [0,0,0,0,0,0,1]])
        self.kf.H = np.array([[1,0,0,0,0,0,0],
                              [0,1,0,0,0,0,0],
                              [0,0,1,0,0,0,0],
                              [0,0,0,1,0,0,0]])
        self.kf.R[2:,2:] *= 10. #4 x 4
        self.kf.P[4:,4:] *= 1000. #give high uncertainty to the unobservable initial velocities
        self.kf.P *= 10. #7 x 7
        self.kf.Q[-1,-1] *= 0.01 #7 x 7
        self.kf.Q[4:,4:] *= 0.01
        self.kf.x[:4] = xyxy_to_xyar(bbox).reshape(4, 1) #7 x 1
    
    @property
    def wasupdated(self):
        return self.time_since_update < 1
    
    @property
    def state(self):
        state = xyar_to_xyxy(self.kf.x[:, 0])
        return state

    def update(self, bbox):
        self.time_since_update = 0
        self.hit_streak += 1
        bbox = xyxy_to_xyar(bbox)
        self.kf.update(bbox)

    def predict(self):
        if self.kf.x[6] + self.kf.x[2] <= 0:
            self.kf.x[6] *= 0.0
        self.kf.predict()
        
        self.age += 1
        if self.time_since_update > 0:
            self.hit_streak = 0
        self.time_since_update += 1