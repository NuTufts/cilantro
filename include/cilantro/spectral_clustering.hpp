#pragma once

#include <memory>
#include <cilantro/3rd_party/spectra/SymEigsSolver.h>
#include <cilantro/3rd_party/spectra/SymGEigsSolver.h>
#include <cilantro/spectral_embedding.hpp>
#include <cilantro/kmeans.hpp>

namespace cilantro {
    namespace internal {
        template <typename ScalarT>
        class SpectraDiagonalInverseBop {
        private:
            const int dim_;
            Eigen::Matrix<ScalarT,Eigen::Dynamic,1> diag_vals_;
            Eigen::Matrix<ScalarT,Eigen::Dynamic,1> diag_vals_inv_;
        public:
            SpectraDiagonalInverseBop(const Eigen::Matrix<ScalarT,Eigen::Dynamic,Eigen::Dynamic>& D)
                    : dim_(D.rows()),
                      diag_vals_(D.diagonal()),
                      diag_vals_inv_(diag_vals_.cwiseInverse())
            {}

            SpectraDiagonalInverseBop(const Eigen::SparseMatrix<ScalarT>& D)
                    : dim_(D.rows()),
                      diag_vals_(D.diagonal()),
                      diag_vals_inv_(diag_vals_.cwiseInverse())
            {}

            inline int rows() const { return dim_; }
            inline int cols() const { return dim_; }

            // y_out = inv(B) * x_in
            void solve(const ScalarT* x_in, ScalarT* y_out) const {
                Eigen::Map<const Eigen::Matrix<ScalarT,Eigen::Dynamic,1>> x(x_in, dim_);
                Eigen::Map<Eigen::Matrix<ScalarT,Eigen::Dynamic,1>> y(y_out, dim_);
                y.noalias() = diag_vals_inv_.cwiseProduct(x);
            }

            // y_out = B * x_in
            void mat_prod(const ScalarT* x_in, ScalarT* y_out) const {
                Eigen::Map<const Eigen::Matrix<ScalarT,Eigen::Dynamic,1>> x(x_in, dim_);
                Eigen::Map<Eigen::Matrix<ScalarT,Eigen::Dynamic,1>> y(y_out, dim_);
                y.noalias() = diag_vals_.cwiseProduct(x);
            }
        };
    }

    enum struct GraphLaplacianType {UNNORMALIZED, NORMALIZED_SYMMETRIC, NORMALIZED_RANDOM_WALK};

    template <class VectorT>
    size_t estimateNumberOfClustersEigengap(const VectorT &eigenvalues,
                                            size_t max_num_clusters)
    {
        typedef typename VectorT::Scalar ScalarT;

        ScalarT min_val = std::numeric_limits<ScalarT>::infinity();
        ScalarT max_val = -std::numeric_limits<ScalarT>::infinity();
        ScalarT max_diff = eigenvalues[0];
        size_t max_ind = 0;
        for (size_t i = 0; i + 1 < eigenvalues.rows(); i++) {
            ScalarT diff = eigenvalues[i+1] - eigenvalues[i];
            if (diff > max_diff) {
                max_diff = diff;
                max_ind = i;
            }
            if (eigenvalues[i] < min_val) min_val = eigenvalues[i];
            if (eigenvalues[i] > max_val) max_val = eigenvalues[i];
        }
        if (eigenvalues[eigenvalues.rows()-1] < min_val) min_val = eigenvalues[eigenvalues.rows()-1];
        if (eigenvalues[eigenvalues.rows()-1] > max_val) max_val = eigenvalues[eigenvalues.rows()-1];

        if (max_val - min_val < std::numeric_limits<ScalarT>::epsilon()) return max_num_clusters;
        return max_ind + 1;
    }

    // If positive, EigenDim is the embedding dimension (and also the number of clusters).
    // Set to Eigen::Dynamic for runtime setting.
    template <typename ScalarT, ptrdiff_t EigenDim = Eigen::Dynamic>
    void computeLaplacianSpectralEmbedding(const Eigen::Ref<const Eigen::Matrix<ScalarT,Eigen::Dynamic,Eigen::Dynamic>> &affinities,
                                           size_t max_num_clusters,
                                           bool estimate_num_clusters,
                                           const GraphLaplacianType &laplacian_type,
                                           VectorSet<ScalarT,EigenDim> &embedded_points,
                                           Vector<ScalarT,EigenDim> &computed_eigenvalues)
    {
        size_t num_clusters = max_num_clusters;
        const size_t num_eigenvalues = (estimate_num_clusters) ? std::min(max_num_clusters+1, (size_t)(affinities.rows()-1)) : max_num_clusters;

        ScalarT conv_tol = (std::is_same<ScalarT,float>::value) ? 1e-7 : 1e-10;
        size_t n_conv = 0;
        size_t max_iter = 1000;

        switch (laplacian_type) {
            case GraphLaplacianType::UNNORMALIZED: {
                Eigen::Matrix<ScalarT,Eigen::Dynamic,Eigen::Dynamic> L = affinities.colwise().sum().asDiagonal();
                L -=  affinities;

                Spectra::DenseSymMatProd<ScalarT> op(L);
                Spectra::SymEigsSolver<ScalarT,Spectra::SMALLEST_MAGN,Spectra::DenseSymMatProd<ScalarT>> eig(&op, num_eigenvalues, std::min(2*num_eigenvalues, (size_t)affinities.rows()));
                eig.init();
                do {
                    n_conv = eig.compute(max_iter, conv_tol, Spectra::SMALLEST_MAGN);
                    max_iter *= 2;
                } while (n_conv != num_eigenvalues);

                computed_eigenvalues = eig.eigenvalues();
                for (size_t i = 0; i < computed_eigenvalues.rows(); i++) {
                    if (computed_eigenvalues[i] < (ScalarT)0.0) computed_eigenvalues[i] = (ScalarT)0.0;
                }

                if (estimate_num_clusters) {
                    num_clusters = estimateNumberOfClustersEigengap(computed_eigenvalues, max_num_clusters);
                }

                embedded_points = eig.eigenvectors(num_clusters).transpose();

                break;
            }
            case GraphLaplacianType::NORMALIZED_SYMMETRIC: {
                Eigen::Matrix<ScalarT,Eigen::Dynamic,Eigen::Dynamic> Dtm12 = affinities.colwise().sum().array().rsqrt().matrix().asDiagonal();
                Eigen::Matrix<ScalarT,Eigen::Dynamic,Eigen::Dynamic> L = Eigen::Matrix<ScalarT,Eigen::Dynamic,Eigen::Dynamic>::Identity(affinities.rows(),affinities.cols()) - Dtm12*affinities*Dtm12;

                Spectra::DenseSymMatProd<ScalarT> op(L);
                Spectra::SymEigsSolver<ScalarT,Spectra::SMALLEST_MAGN,Spectra::DenseSymMatProd<ScalarT>> eig(&op, num_eigenvalues, std::min(2*num_eigenvalues, (size_t)affinities.rows()));
                eig.init();
                do {
                    n_conv = eig.compute(max_iter, conv_tol, Spectra::SMALLEST_MAGN);
                    max_iter *= 2;
                } while (n_conv != num_eigenvalues);

                computed_eigenvalues = eig.eigenvalues();
                for (size_t i = 0; i < computed_eigenvalues.rows(); i++) {
                    if (computed_eigenvalues[i] < (ScalarT)0.0) computed_eigenvalues[i] = (ScalarT)0.0;
                }

                if (estimate_num_clusters) {
                    num_clusters = estimateNumberOfClustersEigengap(computed_eigenvalues, max_num_clusters);
                }

                embedded_points = eig.eigenvectors(num_clusters).transpose();

                for (size_t i = 0; i < embedded_points.cols(); i++) {
                    ScalarT scale = (ScalarT)(1.0)/embedded_points.col(i).norm();
                    if (std::isfinite(scale)) embedded_points.col(i) *= scale;
                }

                break;
            }
            case GraphLaplacianType::NORMALIZED_RANDOM_WALK: {
                Eigen::Matrix<ScalarT,Eigen::Dynamic,Eigen::Dynamic> D = affinities.colwise().sum().asDiagonal();
                Eigen::Matrix<ScalarT,Eigen::Dynamic,Eigen::Dynamic> L = D - affinities;

                Spectra::DenseSymMatProd<ScalarT> op(L);
                internal::SpectraDiagonalInverseBop<ScalarT> Bop(D);
                Spectra::SymGEigsSolver<ScalarT,Spectra::SMALLEST_MAGN,Spectra::DenseSymMatProd<ScalarT>,internal::SpectraDiagonalInverseBop<ScalarT>,Spectra::GEIGS_REGULAR_INVERSE> eig(&op, &Bop, num_eigenvalues, std::min(2*num_eigenvalues, (size_t)affinities.rows()));

                eig.init();
                do {
                    n_conv = eig.compute(max_iter, conv_tol, Spectra::SMALLEST_MAGN);
                    max_iter *= 2;
                } while (n_conv != num_eigenvalues);

                computed_eigenvalues = eig.eigenvalues();
                for (size_t i = 0; i < computed_eigenvalues.rows(); i++) {
                    if (computed_eigenvalues[i] < (ScalarT)0.0) computed_eigenvalues[i] = (ScalarT)0.0;
                }

                if (estimate_num_clusters) {
                    num_clusters = estimateNumberOfClustersEigengap(computed_eigenvalues, max_num_clusters);
                }

                embedded_points = eig.eigenvectors(num_clusters).transpose();

                break;
            }
        }
    }

    // If positive, EigenDim is the embedding dimension (and also the number of clusters).
    // Set to Eigen::Dynamic for runtime setting.
    template <typename ScalarT, ptrdiff_t EigenDim = Eigen::Dynamic>
    inline void computeLaplacianSpectralEmbedding(const Eigen::Ref<const Eigen::Matrix<ScalarT,Eigen::Dynamic,Eigen::Dynamic>> &affinities,
                                                  size_t max_num_clusters,
                                                  bool estimate_num_clusters,
                                                  const GraphLaplacianType &laplacian_type,
                                                  SpectralEmbedding<ScalarT,EigenDim> &embedding)
    {
        computeLaplacianSpectralEmbedding<ScalarT,EigenDim>(affinities, max_num_clusters, estimate_num_clusters, laplacian_type, embedding.embeddedPoints, embedding.computedEigenvalues);
    }

    // If positive, EigenDim is the embedding dimension (and also the number of clusters).
    // Set to Eigen::Dynamic for runtime setting.
    template <typename ScalarT, ptrdiff_t EigenDim = Eigen::Dynamic>
    inline SpectralEmbedding<ScalarT,EigenDim> computeLaplacianSpectralEmbedding(const Eigen::Ref<const Eigen::Matrix<ScalarT,Eigen::Dynamic,Eigen::Dynamic>> &affinities,
                                                                                 size_t max_num_clusters,
                                                                                 bool estimate_num_clusters,
                                                                                 const GraphLaplacianType &laplacian_type)
    {
        SpectralEmbedding<ScalarT,EigenDim> embedding;
        computeLaplacianSpectralEmbedding<ScalarT,EigenDim>(affinities, max_num_clusters, estimate_num_clusters, laplacian_type, embedding.embeddedPoints, embedding.computedEigenvalues);
        return embedding;
    }

    // If positive, EigenDim is the embedding dimension (and also the number of clusters).
    // Set to Eigen::Dynamic for runtime setting.
    template <typename ScalarT, ptrdiff_t EigenDim = Eigen::Dynamic>
    void computeLaplacianSpectralEmbedding(const Eigen::SparseMatrix<ScalarT> &affinities,
                                           size_t max_num_clusters,
                                           bool estimate_num_clusters,
                                           const GraphLaplacianType &laplacian_type,
                                           VectorSet<ScalarT,EigenDim> &embedded_points,
                                           Vector<ScalarT,EigenDim> &computed_eigenvalues)
    {
        size_t num_clusters = max_num_clusters;
        const size_t num_eigenvalues = (estimate_num_clusters) ? std::min(max_num_clusters+1, (size_t)(affinities.rows()-1)) : max_num_clusters;

        ScalarT conv_tol = (std::is_same<ScalarT,float>::value) ? 1e-7 : 1e-10;
        size_t n_conv = 0;
        size_t max_iter = 1000;

        switch (laplacian_type) {
            case GraphLaplacianType::UNNORMALIZED: {
                Eigen::SparseMatrix<ScalarT> D(affinities.rows(),affinities.cols());
                D.reserve(Eigen::VectorXi::Ones(affinities.rows()));
                for (size_t i = 0; i < affinities.cols(); i++) {
                    D.insert(i,i) = affinities.col(i).sum();
                }
                Eigen::SparseMatrix<ScalarT> L = D - affinities;

                Spectra::SparseSymMatProd<ScalarT> op(L);
                Spectra::SymEigsSolver<ScalarT,Spectra::SMALLEST_MAGN,Spectra::SparseSymMatProd<ScalarT>> eig(&op, num_eigenvalues, std::min(2*num_eigenvalues, (size_t)affinities.rows()));
                eig.init();
                do {
                    n_conv = eig.compute(max_iter, conv_tol, Spectra::SMALLEST_MAGN);
                    max_iter *= 2;
                } while (n_conv != num_eigenvalues);

                computed_eigenvalues = eig.eigenvalues();
                for (size_t i = 0; i < computed_eigenvalues.rows(); i++) {
                    if (computed_eigenvalues[i] < (ScalarT)0.0) computed_eigenvalues[i] = (ScalarT)0.0;
                }

                if (estimate_num_clusters) {
                    num_clusters = estimateNumberOfClustersEigengap(computed_eigenvalues, max_num_clusters);
                }

                embedded_points = eig.eigenvectors(num_clusters).transpose();

                break;
            }
            case GraphLaplacianType::NORMALIZED_SYMMETRIC: {
                Eigen::SparseMatrix<ScalarT> Dtm12(affinities.rows(),affinities.cols());
                Dtm12.reserve(Eigen::VectorXi::Ones(affinities.rows()));
                for (size_t i = 0; i < affinities.cols(); i++) {
                    Dtm12.insert(i,i) = (ScalarT)(1.0)/std::sqrt(affinities.col(i).sum());
                }
                Eigen::SparseMatrix<ScalarT> L(affinities.rows(),affinities.cols());
                L.setIdentity();
                L -= Dtm12*affinities*Dtm12;

                Spectra::SparseSymMatProd<ScalarT> op(L);
                Spectra::SymEigsSolver<ScalarT,Spectra::SMALLEST_MAGN,Spectra::SparseSymMatProd<ScalarT>> eig(&op, num_eigenvalues, std::min(2*num_eigenvalues, (size_t)affinities.rows()));
                eig.init();
                do {
                    n_conv = eig.compute(max_iter, conv_tol, Spectra::SMALLEST_MAGN);
                    max_iter *= 2;
                } while (n_conv != num_eigenvalues);

                computed_eigenvalues = eig.eigenvalues();
                for (size_t i = 0; i < computed_eigenvalues.rows(); i++) {
                    if (computed_eigenvalues[i] < (ScalarT)0.0) computed_eigenvalues[i] = (ScalarT)0.0;
                }

                if (estimate_num_clusters) {
                    num_clusters = estimateNumberOfClustersEigengap(computed_eigenvalues, max_num_clusters);
                }

                embedded_points = eig.eigenvectors(num_clusters).transpose();

                for (size_t i = 0; i < embedded_points.cols(); i++) {
                    ScalarT scale = (ScalarT)(1.0)/embedded_points.col(i).norm();
                    if (std::isfinite(scale)) embedded_points.col(i) *= scale;
                }

                break;
            }
            case GraphLaplacianType::NORMALIZED_RANDOM_WALK: {
                Eigen::SparseMatrix<ScalarT> D(affinities.rows(),affinities.cols());
                D.reserve(Eigen::VectorXi::Ones(affinities.rows()));
                for (size_t i = 0; i < affinities.cols(); i++) {
                    D.insert(i,i) = affinities.col(i).sum();
                }
                Eigen::SparseMatrix<ScalarT> L = D - affinities;

                Spectra::SparseSymMatProd<ScalarT> op(L);
                internal::SpectraDiagonalInverseBop<ScalarT> Bop(D);
                Spectra::SymGEigsSolver<ScalarT,Spectra::SMALLEST_MAGN,Spectra::SparseSymMatProd<ScalarT>,internal::SpectraDiagonalInverseBop<ScalarT>,Spectra::GEIGS_REGULAR_INVERSE> eig(&op, &Bop, num_eigenvalues, std::min(2*num_eigenvalues, (size_t)affinities.rows()));

                eig.init();
                do {
                    n_conv = eig.compute(max_iter, conv_tol, Spectra::SMALLEST_MAGN);
                    max_iter *= 2;
                } while (n_conv != num_eigenvalues);

                computed_eigenvalues = eig.eigenvalues();
                for (size_t i = 0; i < computed_eigenvalues.rows(); i++) {
                    if (computed_eigenvalues[i] < (ScalarT)0.0) computed_eigenvalues[i] = (ScalarT)0.0;
                }

                if (estimate_num_clusters) {
                    num_clusters = estimateNumberOfClustersEigengap(computed_eigenvalues, max_num_clusters);
                }

                embedded_points = eig.eigenvectors(num_clusters).transpose();

                break;
            }
        }
    }

    // If positive, EigenDim is the embedding dimension (and also the number of clusters).
    // Set to Eigen::Dynamic for runtime setting.
    template <typename ScalarT, ptrdiff_t EigenDim = Eigen::Dynamic>
    inline void computeLaplacianSpectralEmbedding(const Eigen::SparseMatrix<ScalarT> &affinities,
                                                  size_t max_num_clusters,
                                                  bool estimate_num_clusters,
                                                  const GraphLaplacianType &laplacian_type,
                                                  SpectralEmbedding<ScalarT,EigenDim> &embedding)
    {
        computeLaplacianSpectralEmbedding<ScalarT,EigenDim>(affinities, max_num_clusters, estimate_num_clusters, laplacian_type, embedding.embeddedPoints, embedding.computedEigenvalues);
    }

    // If positive, EigenDim is the embedding dimension (and also the number of clusters).
    // Set to Eigen::Dynamic for runtime setting.
    template <typename ScalarT, ptrdiff_t EigenDim = Eigen::Dynamic>
    inline SpectralEmbedding<ScalarT,EigenDim> computeLaplacianSpectralEmbedding(const Eigen::SparseMatrix<ScalarT> &affinities,
                                                                                 size_t max_num_clusters,
                                                                                 bool estimate_num_clusters,
                                                                                 const GraphLaplacianType &laplacian_type)
    {
        SpectralEmbedding<ScalarT,EigenDim> embedding;
        computeLaplacianSpectralEmbedding<ScalarT,EigenDim>(affinities, max_num_clusters, estimate_num_clusters, laplacian_type, embedding.embeddedPoints, embedding.computedEigenvalues);
        return embedding;
    }

    // If positive, EigenDim is the embedding dimension (and also the number of clusters).
    // Set to Eigen::Dynamic for runtime setting.
    template <typename ScalarT, ptrdiff_t EigenDim = Eigen::Dynamic>
    class SpectralClustering : public SpectralEmbedding<ScalarT,EigenDim>, public KMeans<ScalarT,EigenDim,KDTreeDistanceAdaptors::L2> {
    public:
        EIGEN_MAKE_ALIGNED_OPERATOR_NEW

        typedef ScalarT Scalar;

        enum { EmbeddingDimension = EigenDim };

        typedef KMeans<ScalarT,EigenDim,KDTreeDistanceAdaptors::L2> Clusterer;

        // Dense input
        // Number of clusters (embedding dimension) set at compile time (EigenDim template parameter)
        template <ptrdiff_t Dim = EigenDim, class = typename std::enable_if<Dim != Eigen::Dynamic>::type>
        SpectralClustering(const Eigen::Ref<const Eigen::Matrix<ScalarT,Eigen::Dynamic,Eigen::Dynamic>> &affinities,
                           const GraphLaplacianType &laplacian_type = GraphLaplacianType::NORMALIZED_RANDOM_WALK,
                           size_t kmeans_max_iter = 100,
                           ScalarT kmeans_conv_tol = std::numeric_limits<ScalarT>::epsilon(),
                           bool kmeans_use_kd_tree = false)
                : SpectralEmbedding<ScalarT,EigenDim>(std::move(computeLaplacianSpectralEmbedding<ScalarT,EigenDim>(affinities, EigenDim, false, laplacian_type))),
                  KMeans<ScalarT,EigenDim,KDTreeDistanceAdaptors::L2>(this->embeddedPoints)
        {
            this->cluster(this->embeddedPoints.rows(), kmeans_max_iter, kmeans_conv_tol, kmeans_use_kd_tree);
        }

        // Dense input
        // Number of clusters (embedding dimension) set at runtime (EigenDim == Eigen::Dynamic)
        // If estimate_num_clusters == true, chooses number of clusters in [1, max_num_clusters] based on eigenvalue
        // distribution. Otherwise, returns max_num_clusters clusters.
        template <ptrdiff_t Dim = EigenDim, class = typename std::enable_if<Dim == Eigen::Dynamic>::type>
        SpectralClustering(const Eigen::Ref<const Eigen::Matrix<ScalarT,Eigen::Dynamic,Eigen::Dynamic>> &affinities,
                           size_t max_num_clusters,
                           bool estimate_num_clusters = false,
                           const GraphLaplacianType &laplacian_type = GraphLaplacianType::NORMALIZED_RANDOM_WALK,
                           size_t kmeans_max_iter = 100,
                           ScalarT kmeans_conv_tol = std::numeric_limits<ScalarT>::epsilon(),
                           bool kmeans_use_kd_tree = false)
                : SpectralEmbedding<ScalarT,EigenDim>((max_num_clusters > 0 && max_num_clusters < affinities.rows()) ? std::move(computeLaplacianSpectralEmbedding<ScalarT,EigenDim>(affinities, max_num_clusters, estimate_num_clusters, laplacian_type))
                                                                                                                     : std::move(computeLaplacianSpectralEmbedding<ScalarT,EigenDim>(affinities, 2, false, laplacian_type))),
                  KMeans<ScalarT,EigenDim,KDTreeDistanceAdaptors::L2>(this->embeddedPoints)
        {
            this->cluster(this->embeddedPoints.rows(), kmeans_max_iter, kmeans_conv_tol, kmeans_use_kd_tree);
        }

        // Sparse input
        // Number of clusters (embedding dimension) set at compile time (EigenDim template parameter)
        template <ptrdiff_t Dim = EigenDim, class = typename std::enable_if<Dim != Eigen::Dynamic>::type>
        SpectralClustering(const Eigen::SparseMatrix<ScalarT> &affinities,
                           const GraphLaplacianType &laplacian_type = GraphLaplacianType::NORMALIZED_RANDOM_WALK,
                           size_t kmeans_max_iter = 100,
                           ScalarT kmeans_conv_tol = std::numeric_limits<ScalarT>::epsilon(),
                           bool kmeans_use_kd_tree = false)
                : SpectralEmbedding<ScalarT,EigenDim>(std::move(computeLaplacianSpectralEmbedding<ScalarT,EigenDim>(affinities, EigenDim, false, laplacian_type))),
                  KMeans<ScalarT,EigenDim,KDTreeDistanceAdaptors::L2>(this->embeddedPoints)
        {
            this->cluster(this->embeddedPoints.rows(), kmeans_max_iter, kmeans_conv_tol, kmeans_use_kd_tree);
        }

        // Sparse input
        // Number of clusters (embedding dimension) set at runtime (EigenDim == Eigen::Dynamic)
        // If estimate_num_clusters == true, chooses number of clusters in [1, max_num_clusters] based on eigenvalue
        // distribution. Otherwise, returns max_num_clusters clusters.
        template <ptrdiff_t Dim = EigenDim, class = typename std::enable_if<Dim == Eigen::Dynamic>::type>
        SpectralClustering(const Eigen::SparseMatrix<ScalarT> &affinities,
                           size_t max_num_clusters,
                           bool estimate_num_clusters = false,
                           const GraphLaplacianType &laplacian_type = GraphLaplacianType::NORMALIZED_RANDOM_WALK,
                           size_t kmeans_max_iter = 100,
                           ScalarT kmeans_conv_tol = std::numeric_limits<ScalarT>::epsilon(),
                           bool kmeans_use_kd_tree = false)
                : SpectralEmbedding<ScalarT,EigenDim>((max_num_clusters > 0 && max_num_clusters < affinities.rows()) ? std::move(computeLaplacianSpectralEmbedding<ScalarT,EigenDim>(affinities, max_num_clusters, estimate_num_clusters, laplacian_type))
                                                                                                                     : std::move(computeLaplacianSpectralEmbedding<ScalarT,EigenDim>(affinities, 2, false, laplacian_type))),
                  KMeans<ScalarT,EigenDim,KDTreeDistanceAdaptors::L2>(this->embeddedPoints)
        {
            this->cluster(this->embeddedPoints.rows(), kmeans_max_iter, kmeans_conv_tol, kmeans_use_kd_tree);
        }
    };

    typedef SpectralClustering<float,2> SpectralClustering2f;
    typedef SpectralClustering<double,2> SpectralClustering2d;
    typedef SpectralClustering<float,3> SpectralClustering3f;
    typedef SpectralClustering<double,3> SpectralClustering3d;
    typedef SpectralClustering<float,Eigen::Dynamic> SpectralClusteringXf;
    typedef SpectralClustering<double,Eigen::Dynamic> SpectralClusteringXd;
}
