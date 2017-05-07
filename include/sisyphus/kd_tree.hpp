#pragma once

#include <nanoflann/nanoflann.hpp>
#include <sisyphus/point_cloud.hpp>

class KDTree {
public:
    KDTree(const std::vector<Eigen::Vector3f> &points, size_t max_leaf_size = 10);
    KDTree(const PointCloud &cloud, size_t max_leaf_size = 10);
    ~KDTree();

    void kNearestNeighbors(const Eigen::Vector3f &query_pt, size_t k, std::vector<size_t> &neighbors, std::vector<float> &distances);
    void nearestNeighborsInRadius(const Eigen::Vector3f &query_pt, float radius, std::vector<size_t> &neighbors, std::vector<float> &distances);
    void kNearestNeighborsInRadius(const Eigen::Vector3f &query_pt, size_t k, float radius, std::vector<size_t> &neighbors, std::vector<float> &distances);

private:
    // std::vector<Eigen::Vector3f> to kd-tree adaptor class
    struct VectorOfEigenVectorsAdaptor_ {
        typedef float coord_t;

        // A const ref to the data set origin
        const std::vector<Eigen::Vector3f> &obj;

        /// The constructor that sets the data set source
        VectorOfEigenVectorsAdaptor_(const std::vector<Eigen::Vector3f> &obj_) : obj(obj_) { }

        /// CRTP helper method
        inline const std::vector<Eigen::Vector3f>& derived() const { return obj; }

        // Must return the number of data points
        inline size_t kdtree_get_point_count() const { return derived().size(); }

        // Returns the distance between the vector "p1[0:size-1]" and the data point with index "idx_p2" stored in the class:
        inline coord_t kdtree_distance(const coord_t *p1, const size_t idx_p2,size_t /*size*/) const
        {
            const coord_t d0=p1[0]-derived()[idx_p2](0);
            const coord_t d1=p1[1]-derived()[idx_p2](1);
            const coord_t d2=p1[2]-derived()[idx_p2](2);
            return d0*d0+d1*d1+d2*d2;
        }

        // Returns the dim'th component of the idx'th point in the class:
        // Since this is inlined and the "dim" argument is typically an immediate value, the
        //  "if/else's" are actually solved at compile time.
        inline coord_t kdtree_get_pt(const size_t idx, int dim) const
        {
            return derived()[idx](dim);
        }

        // Optional bounding-box computation: return false to default to a standard bbox computation loop.
        //   Return true if the BBOX was already computed by the class and returned in "bb" so it can be avoided to redo it again.
        //   Look at bb.size() to find out the expected dimensionality (e.g. 2 or 3 for point clouds)
        template <class BBOX>
        bool kdtree_get_bbox(BBOX& /*bb*/) const { return false; }
    };

    typedef nanoflann::KDTreeSingleIndexAdaptor<nanoflann::L2_Simple_Adaptor<float, VectorOfEigenVectorsAdaptor_>, VectorOfEigenVectorsAdaptor_, 3> TreeType_;

    VectorOfEigenVectorsAdaptor_ pcd_to_kd_;
    TreeType_ *kd_tree_;
};