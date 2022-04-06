import numpy as np
import cv2
import json

CLASSES = (
    'person', 'bicycle', 'car', 'motorcycle', 'airplane', 'bus',
    'train', 'truck', 'boat', 'traffic light', 'fire hydrant',
    'stop sign', 'parking meter', 'bench', 'bird', 'cat', 'dog',
    'horse', 'sheep', 'cow', 'elephant', 'bear', 'zebra', 'giraffe', 
    'backpack', 'umbrella', 'handbag', 'tie', 'suitcase', 'frisbee', 
    'skis', 'snowboard', 'sports ball', 'kite', 'baseball bat',
    'baseball glove', 'skateboard', 'surfboard', 'tennis racket',
    'bottle', 'wine glass', 'cup', 'fork', 'knife', 'spoon', 'bowl',
    'banana', 'apple', 'sandwich', 'orange', 'broccoli', 'carrot',
    'hot dog', 'pizza', 'donut', 'cake', 'chair', 'couch',
    'potted plant', 'bed', 'dining table', 'toilet', 'tv', 'laptop',
    'mouse', 'remote', 'keyboard', 'cell phone', 'microwave',
    'oven', 'toaster', 'sink', 'refrigerator', 'book', 'clock',
    'vase', 'scissors', 'teddy bear', 'hair drier', 'toothbrush'
)

#should be called after pruning
#writes the dets to json in COCO format
#note that bbox format is xywh rather than xyxy
def write_dets(dets, fname, frame_id=0):
    results = []
    for det in dets:
        b0, b1, b2, b3 = det[0:4]
        x, y = float(b0), float(b1)
        w, h = float(b2 - b0 + 1), float(b3 - b1 + 1)
        results.append({'image_id': frame_id,
                        'category_id': int(det[5]),
                        'bbox': [x, y, w, h],
                        'score': det[4] / 100})
    with open(fname, 'w') as f:
        f.write(json.dumps(results, indent=4))


def prune_dets(dets, score_thres=0.3, valid_classes=CLASSES):
    num_dets = len(dets)
    if num_dets == 0:
        return dets
    score_thres = score_thres * 100
    valid_classes = set(valid_classes) #much faster if in a set
    score_mask = dets[:, 4] >= score_thres
    labels = [CLASSES[d] for d in dets[:, 5]]
    label_mask = [l in valid_classes for l in labels]
    mask = np.array([score_mask[i] and label_mask[i] for i in range(num_dets)])
    #mask = score_mask & label_mask #both must be true
    return dets[mask]

def draw_dets(img, dets, color=(255, 255, 0), istracks=False):
    for det in dets:
        b0, b1, b2, b3 = det[0:4]

        if istracks:
            text = str(det[4])
        else:
            score = det[4]
            label = CLASSES[det[5]]
            text = '%s: %s' % (label, score)

        img = cv2.rectangle(img,
            (b0, b1),
            (b2, b3),
            color,
            thickness=1
        )
        img = cv2.putText(img,
            text,
            (b0, b1 - 4),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.5,
            color,
            1
        )
    return img

def softmax(probs, axis=-1):
    probs = np.exp(probs)
    sums = np.sum(probs, axis=axis, keepdims=True)
    return probs / sums

def nms_boxes(detections, iou_threshold=0.5):
    """Apply the Non-Maximum Suppression (NMS) algorithm on the bounding
    boxes with their confidence scores and return an array with the
    indexes of the bounding boxes we want to keep.

    # Args
        detections: Nx7 numpy arrays of
                    [[x, y, w, h, box_confidence, class_id, class_prob],
                     ......]
    """
    x_coord = detections[:, 0]
    y_coord = detections[:, 1]
    width = detections[:, 2]
    height = detections[:, 3]
    box_confidences = detections[:, 4] * detections[:, 6]

    areas = width * height
    ordered = box_confidences.argsort()[::-1]

    keep = list()
    while ordered.size > 0:
        # Index of the current element:
        i = ordered[0]
        keep.append(i)
        xx1 = np.maximum(x_coord[i], x_coord[ordered[1:]])
        yy1 = np.maximum(y_coord[i], y_coord[ordered[1:]])
        xx2 = np.minimum(x_coord[i] + width[i], x_coord[ordered[1:]] + width[ordered[1:]])
        yy2 = np.minimum(y_coord[i] + height[i], y_coord[ordered[1:]] + height[ordered[1:]])

        width1 = np.maximum(0.0, xx2 - xx1 + 1)
        height1 = np.maximum(0.0, yy2 - yy1 + 1)
        intersection = width1 * height1
        union = (areas[i] + areas[ordered[1:]] - intersection)
        iou = intersection / union
        indexes = np.where(iou <= iou_threshold)[0]
        ordered = ordered[indexes + 1]

    keep = np.array(keep)
    return keep
