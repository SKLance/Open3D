// ----------------------------------------------------------------------------
// -                        Open3D: www.open3d.org                            -
// ----------------------------------------------------------------------------
// The MIT License (MIT)
//
// Copyright (c) 2018 www.open3d.org
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
// ----------------------------------------------------------------------------

#include "open3d/visualization/rendering/Open3DScene.h"

#include <algorithm>

#include "open3d/geometry/PointCloud.h"
#include "open3d/geometry/TriangleMesh.h"
#include "open3d/visualization/rendering/Material.h"
#include "open3d/visualization/rendering/Scene.h"
#include "open3d/visualization/rendering/View.h"

namespace open3d {
namespace visualization {
namespace rendering {

const std::string kAxisObjectName("__axis__");
const std::string kFastModelObjectSuffix("__fast__");
const std::size_t kDownsampleThreshold = 6000000;

namespace {
std::shared_ptr<geometry::TriangleMesh> CreateAxisGeometry(double axis_length) {
    const double sphere_radius = 0.005 * axis_length;
    const double cyl_radius = 0.0025 * axis_length;
    const double cone_radius = 0.0075 * axis_length;
    const double cyl_height = 0.975 * axis_length;
    const double cone_height = 0.025 * axis_length;

    auto mesh_frame = geometry::TriangleMesh::CreateSphere(sphere_radius);
    mesh_frame->ComputeVertexNormals();
    mesh_frame->PaintUniformColor(Eigen::Vector3d(0.5, 0.5, 0.5));

    std::shared_ptr<geometry::TriangleMesh> mesh_arrow;
    Eigen::Matrix4d transformation;

    mesh_arrow = geometry::TriangleMesh::CreateArrow(cyl_radius, cone_radius,
                                                     cyl_height, cone_height);
    mesh_arrow->ComputeVertexNormals();
    mesh_arrow->PaintUniformColor(Eigen::Vector3d(1.0, 0.0, 0.0));
    transformation << 0, 0, 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1;
    mesh_arrow->Transform(transformation);
    *mesh_frame += *mesh_arrow;

    mesh_arrow = geometry::TriangleMesh::CreateArrow(cyl_radius, cone_radius,
                                                     cyl_height, cone_height);
    mesh_arrow->ComputeVertexNormals();
    mesh_arrow->PaintUniformColor(Eigen::Vector3d(0.0, 1.0, 0.0));
    transformation << 0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1;
    mesh_arrow->Transform(transformation);
    *mesh_frame += *mesh_arrow;

    mesh_arrow = geometry::TriangleMesh::CreateArrow(cyl_radius, cone_radius,
                                                     cyl_height, cone_height);
    mesh_arrow->ComputeVertexNormals();
    mesh_arrow->PaintUniformColor(Eigen::Vector3d(0.0, 0.0, 1.0));
    transformation << 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1;
    mesh_arrow->Transform(transformation);
    *mesh_frame += *mesh_arrow;

    // Add UVs because material shader for axes expects them
    mesh_frame->triangle_uvs_.resize(mesh_frame->triangles_.size() * 3,
                                     {0.0, 0.0});

    return mesh_frame;
}

void RecreateAxis(Scene* scene,
                  const geometry::AxisAlignedBoundingBox& bounds,
                  bool enabled) {
    scene->RemoveGeometry(kAxisObjectName);

    // Axes length should be the longer of the bounds extent or 25% of the
    // distance from the origin. The latter is necessary so that the axis is
    // big enough to be visible even if the object is far from the origin.
    // See caterpillar.ply from Tanks & Temples.
    auto axis_length = bounds.GetMaxExtent();
    if (axis_length < 0.001) {  // avoid div by zero errors in CreateAxes()
        axis_length = 1.0;
    }
    axis_length = std::max(axis_length, 0.25 * bounds.GetCenter().norm());
    auto mesh = CreateAxisGeometry(axis_length);
    scene->AddGeometry(kAxisObjectName, *mesh, Material());
    // It looks awkward to have the axis cast a a shadow, and even stranger
    // to receive a shadow.
    scene->GeometryShadows(kAxisObjectName, false, false);
    scene->ShowGeometry(kAxisObjectName, enabled);
}

}  // namespace

Open3DScene::Open3DScene(Renderer& renderer) : renderer_(renderer) {
    scene_ = renderer_.CreateScene();
    auto scene = renderer_.GetScene(scene_);

    RecreateAxis(scene, bounds_, false);
}

Open3DScene::~Open3DScene() {
    ClearGeometry();
    auto scene = renderer_.GetScene(scene_);
    scene->RemoveGeometry(kAxisObjectName);
}

ViewHandle Open3DScene::CreateView() {
    auto scene = renderer_.GetScene(scene_);
    view_ = scene->AddView(0, 0, 1, 1);

    return view_;
}

void Open3DScene::DestroyView(ViewHandle view) {
    auto scene = renderer_.GetScene(scene_);
    scene->RemoveView(view);
}

View* Open3DScene::GetView(ViewHandle view) const {
    auto scene = renderer_.GetScene(scene_);
    return scene->GetView(view);
}

void Open3DScene::ShowSkybox(bool enable) {
    auto scene = renderer_.GetScene(scene_);
    scene->ShowSkybox(enable);
}

void Open3DScene::ShowAxes(bool enable) {
    auto scene = renderer_.GetScene(scene_);
    scene->ShowGeometry(kAxisObjectName, enable);
}

void Open3DScene::ClearGeometry() {
    auto scene = renderer_.GetScene(scene_);
    for (auto& g : geometries_) {
        scene->RemoveGeometry(g.second.name);
        if (!g.second.fast_name.empty()) {
            scene->RemoveGeometry(g.second.fast_name);
        }
    }
    geometries_.clear();
    bounds_ = geometry::AxisAlignedBoundingBox();
    RecreateAxis(scene, bounds_, false);
}

void Open3DScene::AddGeometry(
        const std::string& name,
        std::shared_ptr<const geometry::Geometry3D> geom,
        const Material& mat,
        bool add_downsampled_copy_for_fast_rendering /*= true*/) {
    size_t downsample_threshold = SIZE_MAX;
    std::string fast_name;
    if (add_downsampled_copy_for_fast_rendering) {
        fast_name = name + "." + kFastModelObjectSuffix;
        downsample_threshold = kDownsampleThreshold;
    }

    auto scene = renderer_.GetScene(scene_);
    if (scene->AddGeometry(name, *geom, mat, fast_name, downsample_threshold)) {
        bounds_ += scene->GetGeometryBoundingBox(name);
        GeometryData info(name, "");
        // If the downsampled object got created, add it. It may not have been
        // created if downsampling wasn't enabled or if the object does not meet
        // the threshold.
        if (add_downsampled_copy_for_fast_rendering &&
            scene->HasGeometry(fast_name)) {
            info.fast_name = fast_name;
            geometries_[fast_name] = info;
        }
        geometries_[name] = info;
        SetGeometryToLOD(info, lod_);
    }

    // Bounding box may have changed, force recreation of axes
    RecreateAxis(scene, bounds_, false);
}

void Open3DScene::AddGeometry(
        const std::string& name,
        const tgeometry::PointCloud* geom,
        const Material& mat,
        bool add_downsampled_copy_for_fast_rendering /*= true*/) {
    size_t downsample_threshold = SIZE_MAX;
    std::string fast_name;
    if (add_downsampled_copy_for_fast_rendering) {
        fast_name = name + "." + kFastModelObjectSuffix;
        downsample_threshold = kDownsampleThreshold;
    }

    auto scene = renderer_.GetScene(scene_);
    if (scene->AddGeometry(name, *geom, mat, fast_name, downsample_threshold)) {
        bounds_ += scene->GetGeometryBoundingBox(name);
        GeometryData info(name, "");
        // If the downsampled object got created, add it. It may not have been
        // created if downsampling wasn't enabled or if the object does not meet
        // the threshold.
        if (add_downsampled_copy_for_fast_rendering &&
            scene->HasGeometry(fast_name)) {
            info.fast_name = fast_name;
        }
        geometries_[name] = info;
        SetGeometryToLOD(info, lod_);
    }

    // Bounding box may have changed, force recreation of axes
    RecreateAxis(scene, bounds_, false);
}

void Open3DScene::RemoveGeometry(const std::string& name) {
    auto scene = renderer_.GetScene(scene_);
    auto g = geometries_.find(name);
    if (g != geometries_.end()) {
        scene->RemoveGeometry(name);
        if (!g->second.fast_name.empty()) {
            scene->RemoveGeometry(g->second.fast_name);
        }
        geometries_.erase(name);
    }
}

void Open3DScene::ShowGeometry(const std::string& name, bool show) {
    auto it = geometries_.find(name);
    if (it != geometries_.end()) {
        it->second.visible = show;
        SetGeometryToLOD(it->second, lod_);
    }
}

void Open3DScene::AddModel(const std::string& name,
                           const TriangleMeshModel& model) {
    auto scene = renderer_.GetScene(scene_);
    if (scene->AddGeometry(name, model)) {
        GeometryData info(name, "");
        bounds_ += scene->GetGeometryBoundingBox(name);
        geometries_[name] = info;
        scene->ShowGeometry(name, true);
    }

    // Bounding box may have changed, force recreation of axes
    RecreateAxis(scene, bounds_, false);
}

void Open3DScene::UpdateMaterial(const Material& mat) {
    auto scene = renderer_.GetScene(scene_);
    for (auto& g : geometries_) {
        scene->OverrideMaterial(g.second.name, mat);
        if (!g.second.fast_name.empty()) {
            scene->OverrideMaterial(g.second.fast_name, mat);
        }
    }
}

void Open3DScene::UpdateModelMaterial(const std::string& name,
                                      const TriangleMeshModel& model) {
    auto scene = renderer_.GetScene(scene_);
    scene->RemoveGeometry(name);
    scene->AddGeometry(name, model);
}

std::vector<std::string> Open3DScene::GetGeometries() {
    std::vector<std::string> names;
    names.reserve(geometries_.size());
    for (auto& it : geometries_) {
        names.push_back(it.first);
    }
    return names;
}

void Open3DScene::SetLOD(LOD lod) {
    if (lod != lod_) {
        lod_ = lod;

        for (auto& g : geometries_) {
            SetGeometryToLOD(g.second, lod);
        }
    }
}

void Open3DScene::SetGeometryToLOD(const GeometryData& data, LOD lod) {
    auto scene = renderer_.GetScene(scene_);
    if (data.fast_name.empty()) {
        scene->ShowGeometry(data.name, data.visible);
        return;
    }

    scene->ShowGeometry(data.name, false);
    if (!data.fast_name.empty()) {
        scene->ShowGeometry(data.fast_name, false);
    }
    if (data.visible) {
        if (lod == LOD::HIGH_DETAIL) {
            scene->ShowGeometry(data.name, true);
        } else {
            scene->ShowGeometry(data.fast_name, true);
        }
    }
}

Open3DScene::LOD Open3DScene::GetLOD() const { return lod_; }

Scene* Open3DScene::GetScene() const { return renderer_.GetScene(scene_); }

Camera* Open3DScene::GetCamera() const {
    auto scene = renderer_.GetScene(scene_);
    auto view = scene->GetView(view_);
    return view->GetCamera();
}

Renderer& Open3DScene::GetRenderer() const { return renderer_; }

}  // namespace rendering
}  // namespace visualization
}  // namespace open3d
