#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>
#include <nanoflann.hpp>
#include <vector>
#include <memory>
#include <cmath>

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

namespace py = pybind11;

class DF
{
private:
    std::shared_ptr<py::detail::unchecked_reference<double, 2>> data_access_;

public:
    DF(py::array_t<double> data)
    {
        data_access_ = std::make_shared<py::detail::unchecked_reference<double, 2>>(data.unchecked<2>());
    }

    /*
        functions required by nanoflann
    */
    std::size_t kdtree_get_point_count() const
    {
        return data_access_->shape(0);
    }

    double kdtree_get_pt(const std::size_t idx, const std::size_t dim) const 
    {
        return (*data_access_)(idx, dim);
    }

    template <class BBOX>
    bool kdtree_get_bbox(BBOX&) const 
    { 
        return false; 
    }

    /*
        functions used while twinning  
    */
    const double* get_row(const std::size_t idx) const
    {
        return data_access_->data(idx, 0);
    }

    std::size_t nrow() const
    {
        return data_access_->shape(0);
    }

    std::size_t ncol() const
    {
        return data_access_->shape(1);
    }
};


typedef nanoflann::KDTreeSingleIndexDynamicAdaptor<nanoflann::L2_Adaptor<double, DF>, DF, -1, std::size_t> KDTree;


class Twinning
{
private:
    const std::size_t r_;
    const std::size_t u1_;
    const std::size_t leaf_size_;
    std::shared_ptr<DF> data_;

public:
    Twinning(py::array_t<double> data, std::size_t r, std::size_t u1, std::size_t leaf_size) : 
    r_(r), u1_(u1), leaf_size_(leaf_size)
    {
        data_ = std::make_shared<DF>(data);
    }

    std::vector<std::size_t> twin()
    {
        std::size_t N = data_->nrow();
        std::size_t dim = data_->ncol();

        KDTree tree(dim, *data_, nanoflann::KDTreeSingleIndexAdaptorParams(leaf_size_));
        KDTree original_tree(dim, *data_, nanoflann::KDTreeSingleIndexAdaptorParams(leaf_size_));
        
        
        nanoflann::KNNResultSet<double> resultSet(r_);
        std::size_t *index = new std::size_t[r_];
        double *distance = new double[r_];

        nanoflann::KNNResultSet<double> resultSet_next_u(1);
        std::size_t index_next_u;
        double distance_next_u;

        std::vector<std::size_t> indices;
        indices.reserve(N / r_ + 1);
        std::size_t position = u1_;

        
        while (true) {
            resultSet.init(index, distance);
            tree.findNeighbors(resultSet, data_->get_row(position), nanoflann::SearchParams());

            double max_distance = -1;
            std::size_t farthest_point = 0;

            for (std::size_t i = 0; i < r_; i++) {
                nanoflann::KNNResultSet<double> tempResultSet(1);
                std::size_t temp_index;
                double temp_distance;
                
                tempResultSet.init(&temp_index, &temp_distance);
                
                original_tree.findNeighbors(tempResultSet, data_->get_row(index[i]), nanoflann::SearchParams());

                if (temp_distance > max_distance) {
                    max_distance = temp_distance;
                    farthest_point = index[i];
                }
            }

            indices.push_back(farthest_point);
            
            for (std::size_t i = 0; i < r_; i++) {
                tree.removePoint(index[i]);
            }

            resultSet_next_u.init(&index_next_u, &distance_next_u);
            tree.findNeighbors(resultSet_next_u, data_->get_row(index[r_ - 1]), nanoflann::SearchParams());
            position = index_next_u;

            if (N - indices.size() * r_ <= r_) {
                resultSet.init(index, distance);
                tree.findNeighbors(resultSet, data_->get_row(position), nanoflann::SearchParams());

                double max_distance = -1;
                std::size_t farthest_point_last = 0;

                for (std::size_t i = 0; i < N - indices.size() * r_; i++) {
                    nanoflann::KNNResultSet<double> tempResultSet_last(1);
                    std::size_t temp_index_last;
                    double temp_distance_last;
                    tempResultSet_last.init(&temp_index_last, &temp_distance_last);

                    original_tree.findNeighbors(tempResultSet_last, data_->get_row(index[i]), nanoflann::SearchParams());

                    if (temp_distance_last > max_distance) {
                        max_distance = temp_distance_last;
                        farthest_point_last = index[i];
                    }
                }
                indices.push_back(farthest_point_last);
                break;
            }
        }

        delete[] index;
        delete[] distance;

        return indices;
    }


    std::vector<std::size_t> get_sequence()
    {
        std::size_t N = data_->nrow();
        std::size_t dim = data_->ncol();

        KDTree tree(dim, *data_, nanoflann::KDTreeSingleIndexAdaptorParams(leaf_size_));
        
        nanoflann::KNNResultSet<double> resultSet(r_);
        std::size_t* index = new std::size_t[r_];
        double* distance = new double[r_];

        nanoflann::KNNResultSet<double> resultSet_next_u(1);
        std::size_t index_next_u;
        double distance_next_u;

        std::vector<std::size_t> sequence;
        sequence.reserve(N);
        std::size_t position = u1_;
        
        while(sequence.size() != N)
        {
            if(sequence.size() > N - r_)
            {
                std::size_t r_f = N - sequence.size();
                nanoflann::KNNResultSet<double> resultSet_f(r_f);
                std::size_t* index_f = new std::size_t[r_f];
                double* distance_f = new double[r_f]; 

                resultSet_f.init(index_f, distance_f);
                tree.findNeighbors(resultSet_f, data_->get_row(position), nanoflann::SearchParams());

                for(std::size_t i = 0; i < r_f; i++)
                    sequence.push_back(index_f[i]);

                delete[] index_f;
                delete[] distance_f;

                break;
            }

            resultSet.init(index, distance);
            tree.findNeighbors(resultSet, data_->get_row(position), nanoflann::SearchParams());
            
            for(std::size_t i = 0; i < r_; i++)
            {
                sequence.push_back(index[i]);
                tree.removePoint(index[i]);
            }

            resultSet_next_u.init(&index_next_u, &distance_next_u);
            tree.findNeighbors(resultSet_next_u, data_->get_row(index[r_ - 1]), nanoflann::SearchParams());  
            position = index_next_u;
        }

        delete[] index;
        delete[] distance;

        return sequence;
    }
};


std::vector<std::size_t> twin_cpp(py::array_t<double> data, std::size_t r, std::size_t u1, std::size_t leaf_size) 
{
    Twinning twinning(data, r, u1, leaf_size);
    return twinning.twin();
}


std::vector<std::size_t> multiplet_S3_cpp(py::array_t<double> data, std::size_t n, std::size_t u1, std::size_t leaf_size) 
{
    Twinning twinning(data, n, u1, leaf_size);
    return twinning.get_sequence();
}


double energy_cpp(py::array_t<double> data, py::array_t<double> points)
{
    DF D(data), sp(points);
    std::size_t dim = D.ncol();
    std::size_t N = D.nrow();
    std::size_t n = sp.nrow();

    std::vector<double> ed_1;
    std::vector<double> ed_2;
    ed_1.resize(n);
    ed_2.resize(n);

    #pragma omp parallel for
    for(int i = 0; i < static_cast<int>(n); i++)
    {
        const double* u_i = sp.get_row(i);

        double distance_sum = 0.0;
        double inner_sum = 0.0;
        for(std::size_t j = 0; j < N; j++)
        {
            const double* z_j = D.get_row(j);

            inner_sum = 0.0;
            for(std::size_t k = 0; k < dim; k++)
                inner_sum += std::pow(*(u_i + k) - *(z_j + k), 2);

            distance_sum += std::sqrt(inner_sum);
        }

        ed_1[i] = distance_sum;

        distance_sum = 0.0;
        for(int j = 0; j < static_cast<int>(n); j++)
            if(j != i)
            {
                const double* u_j = sp.get_row(j);

                inner_sum = 0.0;
                for(std::size_t k = 0; k < dim; k++)
                    inner_sum += std::pow(*(u_i + k) - *(u_j + k), 2); 

                distance_sum += std::sqrt(inner_sum);
            }

        ed_2[i] = distance_sum;
    }

    double sum1 = 0.0;
    double sum2 = 0.0;
    for(std::size_t i = 0; i < n; i++)
    {
        sum1 += ed_1[i];
        sum2 += ed_2[i];
    }

    return 2.0 * sum1 / (N * n) - sum2 / (n * n);
}


PYBIND11_MODULE(twinning_cpp, m){
    m.doc() = R"pbdoc(
        .. currentmodule:: twinning_cpp

        .. autosummary::
           :toctree: _generate

           twin_cpp
           multiplet_S3_cpp
           energy_cpp
    )pbdoc";

    m.def("twin_cpp", &twin_cpp, R"pbdoc(
        Partition a dataset into statistically similar twin sets (C++ extension).
    )pbdoc");

    m.def("multiplet_S3_cpp", &multiplet_S3_cpp, R"pbdoc(
        Generate multiplets using strategy 3 (C++ extension).
    )pbdoc");

    m.def("energy_cpp", &energy_cpp, R"pbdoc(
        Energy distance computation (C++ extension).
    )pbdoc");

#ifdef VERSION_INFO
    m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
    m.attr("__version__") = "dev";
#endif
}
