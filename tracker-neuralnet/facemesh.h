/* Copyright (c) 2026 TrackHat
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#pragma once

#include "model_adapters.h"

#include <optional>
#include <vector>

#include <onnxruntime_cxx_api.h>
#include <opencv2/core.hpp>

namespace neuralnet_tracker_ns
{

/** MediaPipe's face detector ("BlazeFace", short range). Finds the face when there is no track.
 */
class FaceDetector
{
public:
    struct Detection
    {
        cv::Rect2f box; // Square around the face
        float roll;     // In-plane rotation of the face. Radians.
    };

    FaceDetector(Ort::MemoryInfo& allocator_info, Ort::Session&& session);
    // Returns the most confident face in `frame` (greyscale), if there is one.
    std::optional<Detection> run(const cv::Mat& frame);
    double last_inference_time_millis() const { return last_inference_time_; }

    static constexpr int INPUT_SIZE = 128;
    static constexpr int NUM_ANCHORS = 896;

private:
    Ort::Session session_{ nullptr };
    mutable Ort::Allocator allocator_;
    std::vector<std::string> input_names_, output_names_;
    std::vector<const char*> input_c_names_, output_c_names_;
    size_t box_output_ = 0, score_output_ = 0;
    std::vector<cv::Point2f> anchors_;
    cv::Mat square_{}, scaled_{}, input_mat_{};
    Ort::Value input_val_{ nullptr };
    double last_inference_time_ = 0;
};

/** Experimental alternative to PoseEstimator: MediaPipe's face landmark model (478 points).
 *
 * The head pose is recovered by fitting MediaPipe's canonical face model to the landmarks
 * (weighted Procrustes analysis). The result is returned as a PoseEstimator::Face so that
 * the rest of the tracker - 3d position, offsets, preview - works unchanged.
 */
class FaceMeshEstimator
{
public:
    // Similarity transform from the canonical face model (cm) to landmark space (pixels).
    struct Fit
    {
        float scale = 0;  // Pixels per centimeter
        cv::Matx33f rotation = cv::Matx33f::eye();
        cv::Vec3f translation = {};
    };

    FaceMeshEstimator(Ort::MemoryInfo& allocator_info, Ort::Session&& session);

    /** Inference
     *
     * `box` is a face bounding box in `frame`: either from the face detector or Face::box from
     * the previous frame. Coordinate conventions are those of PoseEstimator::run.
     */
    std::optional<PoseEstimator::Face> run(const cv::Mat& frame, const cv::Rect2f& box);
    // Sets the in-plane rotation of the face for the next run. Call when the box does not come
    // from this estimator. Otherwise the rotation is carried over from the previous frame.
    void set_roll(float roll) { roll_ = roll; }
    // Returns an image compatible with the 'frame' image for displaying.
    cv::Mat last_network_input() const;
    double last_inference_time_millis() const { return last_inference_time_; }
    cv::Size input_image_size() const { return { INPUT_SIZE, INPUT_SIZE }; }
    // Landmark positions of the last successful run, in coordinates of the frame given to it.
    const std::vector<cv::Point2f>& last_landmarks() const { return landmarks_2d_; }

    // Weighted least-squares fit of the canonical face model to `landmarks` (at least 468 points).
    static std::optional<Fit> fit_canonical_model(const cv::Point3f* landmarks);

    static constexpr int INPUT_SIZE = 256;
    static constexpr int NUM_LANDMARKS = 478;
    // The face box is enlarged by this factor to get the image patch for the model. As in MediaPipe.
    static constexpr float ROI_SCALE = 1.5f;

private:
    Ort::Session session_{ nullptr };
    mutable Ort::Allocator allocator_;
    std::vector<std::string> input_names_, output_names_;
    std::vector<const char*> input_c_names_, output_c_names_;
    size_t landmark_output_ = 0, presence_output_ = 0;

    cv::Mat patch_{}, input_mat_{};
    Ort::Value input_val_{ nullptr };
    std::vector<cv::Point3f> landmarks_;
    std::vector<cv::Point2f> landmarks_2d_;
    float roll_ = 0; // In-plane rotation of the face in the previous frame. Radians.
    double last_inference_time_ = 0;
};

} // namespace neuralnet_tracker_ns
