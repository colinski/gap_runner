import numpy as np
from .kalman_track import KalmanTrack
from .hungarian import match

class SORT(object):
    def __init__(self, max_age=1, min_hits=3, iou_thres=0.3):
        self.max_age = max_age
        self.min_hits = min_hits
        self.iou_thres = iou_thres
        self.tracks = []
        self.frame_count = 0 
    
    def update(self, dets=np.empty((0, 4))):
        self.frame_count += 1
        
        #get predictions from existing tracks
        for track in self.tracks:
            track.predict()
        
        #collect all the new bbox predictions
        preds = np.zeros((0, 4))
        if len(self.tracks) > 0:
            preds = np.array([track.state for track in self.tracks])
        
        #Hungarian algo
        matches, unmatched_dets = match(dets, preds, self.iou_thres)

        #update tracks with matched detections
        for d, t in matches:
            self.tracks[t].update(dets[d])

        #start new track for each unmatched detection
        for d in unmatched_dets:
            new_track = KalmanTrack(dets[d])
            self.tracks.append(new_track)
            
        #collect final outputs
        states, ids = [np.empty((0,4))], []
        #states, ids = [], []
        for track in self.tracks:
            onstreak = track.hit_streak >= self.min_hits
            warmingup = self.frame_count <= self.min_hits
            if track.wasupdated and (onstreak or warmingup):
                states.append(track.state.reshape(1,4))
                ids.append(track.id)
        states = np.concatenate(states)
        ids = np.array(ids).reshape(-1, 1)
        ret = np.concatenate([states, ids], axis=-1)
        
        #remove tracks that have expired
        self.tracks = [track for track in self.tracks\
                       if track.time_since_update < self.max_age]
        return ret.astype(int)
