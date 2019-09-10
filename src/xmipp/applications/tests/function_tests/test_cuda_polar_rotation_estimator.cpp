#include "reconstruction_cuda/gpu.h"
#include "reconstruction_cuda/cuda_rot_polar_estimator.h"

template<typename T>
class ARotationEstimator_Test;

#define SETUP \
    void SetUp() { \
        estimator = new Alignment::CudaRotPolarEstimator<T>(); \
    }

#define SETUPTESTCASE \
    static void SetUpTestCase() { \
        for (int i = 0; i < 2; ++i) { \
            auto g = new GPU(); \
            g->set(); \
            hw.push_back(g); \
        } \
    }

#define INIT \
    estimator->init(hw, AlignType::OneToN, dims, batch, maxRotation); \
    hw.at(0)->lockMemory(others, dims.size() * sizeof(T));

#define TEARDOWN \
    hw.at(0)->unlockMemory(others);

#include "arotation_estimator_tests.h"

typedef ::testing::Types<float, double> TestTypes;
INSTANTIATE_TYPED_TEST_CASE_P(Gpu, ARotationEstimator_Test, TestTypes);
