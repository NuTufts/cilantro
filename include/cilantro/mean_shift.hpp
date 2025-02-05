#pragma once

#include <cilantro/kd_tree.hpp>
#include <cilantro/common_pair_evaluators.hpp>
#include <cilantro/clustering.hpp>

namespace cilantro {
    template <typename ScalarT, ptrdiff_t EigenDim, template <class> class DistAdaptor = KDTreeDistanceAdaptors::L2>
    class MeanShift : public Clustering {
    public:
        EIGEN_MAKE_ALIGNED_OPERATOR_NEW

        typedef ScalarT Scalar;

        enum { Dimension = EigenDim };

        MeanShift(const ConstVectorSetMatrixMap<ScalarT,EigenDim> &points, size_t max_leaf_size = 10)
                : data_map_(points),
                  kd_tree_ptr_(new KDTree<ScalarT,EigenDim,DistAdaptor>(points, max_leaf_size)),
                  kd_tree_owned_(true),
                  iteration_count_(0)
        {}

        MeanShift(const KDTree<ScalarT,EigenDim,DistAdaptor> &kd_tree)
                : data_map_(kd_tree.getPointsMatrixMap()),
                  kd_tree_ptr_(&kd_tree),
                  kd_tree_owned_(false),
                  iteration_count_(0)
        {}

        ~MeanShift() {
            if (kd_tree_owned_) delete kd_tree_ptr_;
        }

        template <class KernelEvaluatorT = UnityWeightEvaluator<ScalarT,ScalarT>>
        MeanShift& cluster(const ConstVectorSetMatrixMap<ScalarT,EigenDim> &seeds,
                           ScalarT kernel_radius,
                           size_t max_iter,
                           ScalarT cluster_tol,
                           ScalarT convergence_tol = std::numeric_limits<ScalarT>::epsilon(),
                           const KernelEvaluatorT &evaluator = KernelEvaluatorT())
        {
            shifted_seeds_ = seeds;

            // Shift points
            const ScalarT radius_sq = kernel_radius*kernel_radius;
            const ScalarT conv_tol_sq = convergence_tol*convergence_tol;
            iteration_count_ = 0;

            std::vector<char> has_converged(shifted_seeds_.cols(), 0);
            bool all_converged;
            NeighborSet<ScalarT> nn;
            Vector<ScalarT,EigenDim> point_tmp;

            while (iteration_count_ < max_iter) {
                all_converged = true;
#pragma omp parallel for shared (has_converged, all_converged) private (nn, point_tmp)
                for (size_t i = 0; i < shifted_seeds_.cols(); i++) {
                    if (has_converged[i]) continue;
                    kd_tree_ptr_->radiusSearch(shifted_seeds_.col(i), radius_sq, nn);
                    point_tmp.setZero(shifted_seeds_.rows(), 1);
                    ScalarT total_weight = (ScalarT)0;
                    for (size_t j = 0; j < nn.size(); j++) {
                        const ScalarT weight = evaluator.template operator()<Eigen::Ref<const Vector<ScalarT,EigenDim>>>(shifted_seeds_.col(i), data_map_.col(nn[j].index), nn[j].value);
                        point_tmp.noalias() += weight*data_map_.col(nn[j].index);
                        total_weight += weight;
                    }
                    point_tmp *= (ScalarT)(1.0)/total_weight;
                    if ((shifted_seeds_.col(i) - point_tmp).squaredNorm() < conv_tol_sq) {
                        has_converged[i] = 1;
                    } else {
                        all_converged = false;
                    }
                    shifted_seeds_.col(i) = point_tmp;
                }

                iteration_count_++;

                if (all_converged) break;
            }

            // Cluster
            const ScalarT cluster_tol_sq = cluster_tol*cluster_tol;

            for (size_t i = 0; i < shifted_seeds_.cols(); i++) {
                size_t c;
                for (c = 0; c < clusterToPointIndicesMap.size(); c++) {
                    if ((shifted_seeds_.col(i) - shifted_seeds_.col(clusterToPointIndicesMap[c][0])).squaredNorm() < cluster_tol_sq) break;
                }

                if (c == clusterToPointIndicesMap.size()) {
                    clusterToPointIndicesMap.emplace_back(1, i);
                } else {
                    clusterToPointIndicesMap[c].emplace_back(i);
                }
            }

            pointToClusterIndexMap.resize(shifted_seeds_.cols());
            cluster_modes_.resize(data_map_.rows(), clusterToPointIndicesMap.size());
            for (size_t i = 0; i < clusterToPointIndicesMap.size(); i++) {
                cluster_modes_.col(i).setZero();
                for (size_t j = 0; j < clusterToPointIndicesMap[i].size(); j++) {
                    cluster_modes_.col(i) += shifted_seeds_.col(clusterToPointIndicesMap[i][j]);
                    pointToClusterIndexMap[clusterToPointIndicesMap[i][j]] = i;
                }
                cluster_modes_.col(i) *= (ScalarT)(1.0)/clusterToPointIndicesMap[i].size();
            }

            return *this;
        }

        template <class KernelEvaluatorT = UnityWeightEvaluator<ScalarT,ScalarT>>
        inline MeanShift& cluster(ScalarT kernel_radius,
                                  size_t max_iter,
                                  ScalarT cluster_tol,
                                  ScalarT convergence_tol = std::numeric_limits<ScalarT>::epsilon(),
                                  const KernelEvaluatorT &evaluator = KernelEvaluatorT())
        {
            return cluster<KernelEvaluatorT>(data_map_, kernel_radius, max_iter, cluster_tol, convergence_tol, evaluator);
        }

        inline const VectorSet<ScalarT,EigenDim>& getShiftedSeeds() const { return shifted_seeds_; }

        inline const VectorSet<ScalarT,EigenDim>& getClusterModes() const { return cluster_modes_; }

        inline size_t getNumberOfPerformedIterations() const { return iteration_count_; }

    private:
        ConstVectorSetMatrixMap<ScalarT,EigenDim> data_map_;
        const KDTree<ScalarT,EigenDim,DistAdaptor> *kd_tree_ptr_;
        bool kd_tree_owned_;
        size_t iteration_count_;

        VectorSet<ScalarT,EigenDim> shifted_seeds_;
        VectorSet<ScalarT,EigenDim> cluster_modes_;
    };

    template <template <class> class DistAdaptor = KDTreeDistanceAdaptors::L2>
    using MeanShift2f = MeanShift<float,2,DistAdaptor>;

    template <template <class> class DistAdaptor = KDTreeDistanceAdaptors::L2>
    using MeanShift2d = MeanShift<double,2,DistAdaptor>;

    template <template <class> class DistAdaptor = KDTreeDistanceAdaptors::L2>
    using MeanShift3f = MeanShift<float,3,DistAdaptor>;

    template <template <class> class DistAdaptor = KDTreeDistanceAdaptors::L2>
    using MeanShift3d = MeanShift<double,3,DistAdaptor>;

    template <template <class> class DistAdaptor = KDTreeDistanceAdaptors::L2>
    using MeanShiftXf = MeanShift<float,Eigen::Dynamic,DistAdaptor>;

    template <template <class> class DistAdaptor = KDTreeDistanceAdaptors::L2>
    using MeanShiftXd = MeanShift<double,Eigen::Dynamic,DistAdaptor>;
}
