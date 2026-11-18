#!/bin/sh
# Rebuilds tracker-neuralnet/models/mediapipe-face-{detector,landmarks}.onnx from MediaPipe's Face Landmarker bundle.
#
# Needs python3 with: pip install tf2onnx tensorflow-cpu onnx
# The bundle and the model inside are Apache 2.0 (see 3rdparty-notices/MEDIAPIPE.txt).
set -e
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
curl -sSL -o "$tmp/face_landmarker.task" \
    https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/1/face_landmarker.task
# The .task file is a zip archive
python3 -c "import zipfile,sys; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$tmp/face_landmarker.task" "$tmp"
python3 -m tf2onnx.convert --tflite "$tmp/face_detector.tflite" --opset 13 \
    --output "$here/../../tracker-neuralnet/models/mediapipe-face-detector.onnx"
python3 -m tf2onnx.convert --tflite "$tmp/face_landmarks_detector.tflite" --opset 13 \
    --output "$here/../../tracker-neuralnet/models/mediapipe-face-landmarks.onnx"
rm -rf "$tmp"
