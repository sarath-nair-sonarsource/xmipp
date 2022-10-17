/***************************************************************************
 *
 * Authors:    David Strelak (davidstrelak@gmail.com)
 *
 * Unidad de  Bioinformatica of Centro Nacional de Biotecnologia , CSIC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307  USA
 *
 *  All comments concerning this program package may be sent to the
 *  e-mail address 'xmipp@cnb.csic.es'
 ***************************************************************************/

#include "reconstruction_adapt_cuda/movie_alignment_correlation_gpu.h"
#include "core/utils/memory_utils.h"
#include <thread>
#include "reconstruction_cuda/cuda_gpu_movie_alignment_correlation.h"
#include "reconstruction_cuda/cuda_gpu_geo_transformer.h"
#include "data/filters.h"
#include "core/userSettings.h"
#include "reconstruction_cuda/cuda_fft.h"
#include "core/utils/time_utils.h"
#include "reconstruction_adapt_cuda/basic_mem_manager.h"
#include "core/xmipp_image_generic.h"

template<typename T>
void ProgMovieAlignmentCorrelationGPU<T>::defineParams() {
    AProgMovieAlignmentCorrelation<T>::defineParams();
    this->addParamsLine("  [--device <dev=0>]                 : GPU device to use. 0th by default");
    this->addParamsLine("  [--storage <fn=\"\">]              : Path to file that can be used to store results of the benchmark");
    this->addParamsLine("  [--patchesAvg <avg=3>]             : Number of near frames used for averaging a single patch");
    this->addParamsLine("  [--skipAutotuning]                 : Skip autotuning of the cuFFT library");
    this->addExampleLine(
                "xmipp_cuda_movie_alignment_correlation -i movie.xmd --oaligned alignedMovie.stk --oavg alignedMicrograph.mrc --device 0");
    this->addSeeAlsoLine("xmipp_movie_alignment_correlation");
}

template<typename T>
void ProgMovieAlignmentCorrelationGPU<T>::show() {
    AProgMovieAlignmentCorrelation<T>::show();
    std::cout << "Device:              " << gpu.value().device() << " (" << gpu.value().getUUID() << ")" << "\n";
    std::cout << "Benchmark storage    " << (storage.empty() ? "Default" : storage) << "\n";
    std::cout << "Patches avg:         " << patchesAvg << "\n";
    std::cout << "Autotuning:          " << (skipAutotuning ? "off" : "on") << std::endl;
}

template<typename T>
void ProgMovieAlignmentCorrelationGPU<T>::readParams() {
    AProgMovieAlignmentCorrelation<T>::readParams();

    // read GPU
    int device = this->getIntParam("--device");
    if (device < 0)
        REPORT_ERROR(ERR_ARG_INCORRECT,
            "Invalid GPU device");
    gpu = core::optional<GPU>(device);
    gpu.value().set();

    // read permanent storage
    storage = this->getParam("--storage");

    skipAutotuning = this->checkParam("--skipAutotuning");

    // read patch averaging
    patchesAvg = this->getIntParam("--patchesAvg");
    if (patchesAvg < 1)
        REPORT_ERROR(ERR_ARG_INCORRECT,
            "Patch averaging has to be at least one.");
}

template<typename T>
FFTSettings<T> ProgMovieAlignmentCorrelationGPU<T>::getSettingsOrBenchmark(
        const Dimensions &d, size_t extraBytes, bool crop) {
    auto optSetting = getStoredSizes(d, crop);
    FFTSettings<T> result =
            optSetting ?
                    optSetting.value() : runBenchmark(d, extraBytes, crop);
    if (!optSetting) {
        storeSizes(d, result, crop);
    }
    return result;
}

template<typename T>
auto ProgMovieAlignmentCorrelationGPU<T>::GlobalAlignmentHelper::findGoodCropSize(const Dimensions &movie, const GPU &gpu) {
    const bool crop = true;
    std::cout << "Benchmarking cuFFT ..." << std::endl;
    auto hint = FFTSettings<T>(movie.createSingle()); // movie frame is big enought to give us an idea
    auto candidate = std::unique_ptr<FFTSettings<T>>(CudaFFT<T>::findOptimal(gpu, hint, 0, hint.sDim().x() == hint.sDim().y(), 10, crop, true));
    if (!candidate) {
        REPORT_ERROR(ERR_GPU_MEMORY, "Insufficient GPU memory for processing a single frame of the movie.");
    }
    return candidate->sDim().copyForN(movie.n());
}

template<typename T>
auto  ProgMovieAlignmentCorrelationGPU<T>::GlobalAlignmentHelper::findGoodCorrelationSize(const Dimensions &hint, const GPU &gpu) {
    const bool crop = false;
    std::cout << "Benchmarking cuFFT ..." << std::endl;
    auto settings = FFTSettings<T>(hint.copyForN((std::ceil(sqrt(hint.n() * 2))))); // test just number of frames, to get an idea (it's faster)
    auto candidate = std::unique_ptr<FFTSettings<T>>(CudaFFT<T>::findOptimal(gpu, settings, 0, settings.sDim().x() == settings.sDim().y(), 20, crop, true));
    if (!candidate) {
        REPORT_ERROR(ERR_GPU_MEMORY, "Insufficient GPU memory for processing a correlations of the movie.");
    }
    return candidate->sDim().copyForN(hint.n());
}

template<typename T>
FFTSettings<T> ProgMovieAlignmentCorrelationGPU<T>::getMovieSettings(
        const MetaData &movie, bool optimize) {
    gpu.value().updateMemoryInfo();
    auto dim = this->getMovieSize();

    if (optimize) {
        size_t maxFilterBytes = getMaxFilterBytes(dim);
        return getSettingsOrBenchmark(dim, maxFilterBytes, true);
    } else {
        return FFTSettings<T>(dim, 1, false);
    }
}

template<typename T>
auto ProgMovieAlignmentCorrelationGPU<T>::getCorrelationHint(
        const Dimensions &d) {
    auto getNearestEven = [this] (size_t v, T minScale, size_t shift) { // scale is less than 1
        size_t size = std::ceil(getCenterSize(shift) / 2.f) * 2; // to get even size
        while ((size / (float)v) < minScale) {
            size += 2;
        }
        return size;
    };
    const T requestedScale = this->getScaleFactor();
    // hint, possibly bigger then requested, so that it fits max shift window
    Dimensions hint(getNearestEven(d.x(), requestedScale, this->maxShift),
            getNearestEven(d.y(), requestedScale, this->maxShift),
            d.z(), (d.n() * (d.n() - 1)) / 2); // number of correlations);
    return hint;
}


template<typename T>
FFTSettings<T> ProgMovieAlignmentCorrelationGPU<T>::getCorrelationSettings(
        const FFTSettings<T> &s) {
    gpu.value().updateMemoryInfo();
    auto getNearestEven = [this] (size_t v, T minScale, size_t shift) { // scale is less than 1
        size_t size = std::ceil(getCenterSize(shift) / 2.f) * 2; // to get even size
        while ((size / (float)v) < minScale) {
            size += 2;
        }
        return size;
    };
    const T requestedScale = this->getScaleFactor();
    // hint, possibly bigger then requested, so that it fits max shift window
    Dimensions hint(getNearestEven(s.sDim().x(), requestedScale, this->maxShift),
            getNearestEven(s.sDim().y(), requestedScale, this->maxShift),
            s.sDim().z(),
            (s.sDim().n() * (s.sDim().n() - 1)) / 2); // number of correlations);

    // divide available memory to 3 parts (2 buffers + 1 FFT)
    size_t correlationBufferBytes = gpu.value().lastFreeBytes() / 3;

    return getSettingsOrBenchmark(hint, 2 * correlationBufferBytes, false);
}

template<typename T>
FFTSettings<T> ProgMovieAlignmentCorrelationGPU<T>::getPatchSettings(
        const FFTSettings<T> &orig) {
    gpu.value().updateMemoryInfo();
    const auto reqSize = this->getRequestedPatchSize();
    Dimensions hint(reqSize.first, reqSize.second,
            orig.sDim().z(), orig.sDim().n());
    // divide available memory to 3 parts (2 buffers + 1 FFT)
    size_t correlationBufferBytes = gpu.value().lastFreeBytes() / 3;

    return getSettingsOrBenchmark(hint, 2 * correlationBufferBytes, false);
}

template<typename T>
std::vector<FramePatchMeta<T>> ProgMovieAlignmentCorrelationGPU<T>::getPatchesLocation(
        const std::pair<T, T> &borders,
        const Dimensions &movie, const Dimensions &patch) {
    size_t patchesX = this->localAlignPatches.first;
    size_t patchesY = this->localAlignPatches.second;
    T windowXSize = movie.x() - 2 * borders.first;
    T windowYSize = movie.y() - 2 * borders.second;
    T corrX = std::ceil(
            ((patchesX * patch.x()) - windowXSize) / (T) (patchesX - 1));
    T corrY = std::ceil(
            ((patchesY * patch.y()) - windowYSize) / (T) (patchesY - 1));
    T stepX = (T)patch.x() - corrX;
    T stepY = (T)patch.y() - corrY;
    std::vector<FramePatchMeta<T>> result;
    for (size_t y = 0; y < patchesY; ++y) {
        for (size_t x = 0; x < patchesX; ++x) {
            T tlx = borders.first + x * stepX; // Top Left
            T tly = borders.second + y * stepY;
            T brx = tlx + patch.x() - 1; // Bottom Right
            T bry = tly + patch.y() - 1; // -1 for indexing
            Point2D<T> tl(tlx, tly);
            Point2D<T> br(brx, bry);
            Rectangle<Point2D<T>> r(tl, br);
            result.emplace_back(
                    FramePatchMeta<T> { .rec = r, .id_x = x, .id_y =
                    y });
        }
    }
    return result;
}

template<typename T>
void ProgMovieAlignmentCorrelationGPU<T>::getPatchData(const Rectangle<Point2D<T>> &patch, 
        const AlignmentResult<T> &globAlignment, T *result) {
    auto &movieDim = movie.getFullDim();
    size_t n = movieDim.n();
    auto patchSize = patch.getSize();
    auto copyPatchData = [&](size_t srcFrameIdx, size_t t, bool add) {
        auto *fullFrame = this->movie.getFullFrame(srcFrameIdx).data;
        size_t patchOffset = t * patchSize.x * patchSize.y;
        // keep the shift consistent while adding local shift
        int xShift = std::round(globAlignment.shifts.at(srcFrameIdx).x);
        int yShift = std::round(globAlignment.shifts.at(srcFrameIdx).y);
        for (size_t y = 0; y < patchSize.y; ++y) {
            size_t srcY = patch.tl.y + y;
            if (yShift < 0) {
                srcY -= (size_t)std::abs(yShift); // assuming shift is smaller than offset
            } else {
                srcY += yShift;
            }
            size_t srcIndex = (srcY * movieDim.x()) + (size_t)patch.tl.x;
            if (xShift < 0) {
                srcIndex -= (size_t)std::abs(xShift);
            } else {
                srcIndex += xShift;
            }
            size_t destIndex = patchOffset + y * patchSize.x;
            if (add) {
                for (size_t x = 0; x < patchSize.x; ++x) {
                    result[destIndex + x] += fullFrame[srcIndex + x];
                }
            } else {
                memcpy(result + destIndex, fullFrame + srcIndex, patchSize.x * sizeof(T));
            }
        }
    };
    for (int t = 0; t < n; ++t) {
        // copy the data from specific frame
        copyPatchData(t, t, false);
        // add data from frames with lower indices
        // while averaging odd num of frames, use copy equally from previous and following frames
        // otherwise prefer following frames
        for (int b = 1; b <= ((patchesAvg - 1) / 2); ++b) {
            if (t >= b) {
                copyPatchData(t - b, t, true);
            }
        }
        // add data from frames with higher indices
        for (int f = 1; f <= (patchesAvg / 2); ++f) {
            if ((t + f) < n) {
                copyPatchData(t + f, t, true);
            }
        }
    }
}

template<typename T>
void ProgMovieAlignmentCorrelationGPU<T>::storeSizes(const Dimensions &dim,
        const FFTSettings<T> &s, bool applyCrop) {
    UserSettings::get(storage).insert(*this,
            getKey(optSizeXStr, dim, applyCrop), s.sDim().x());
    UserSettings::get(storage).insert(*this,
            getKey(optSizeYStr, dim, applyCrop), s.sDim().y());
    UserSettings::get(storage).insert(*this,
            getKey(optBatchSizeStr, dim, applyCrop), s.batch());
    UserSettings::get(storage).insert(*this,
            getKey(minMemoryStr, dim, applyCrop), memoryUtils::MB(gpu.value().lastFreeBytes()));
    UserSettings::get(storage).store(); // write changes immediately
}

template<typename T>
core::optional<FFTSettings<T>> ProgMovieAlignmentCorrelationGPU<T>::getStoredSizes(
        const Dimensions &dim, bool applyCrop) {
    size_t x, y, batch, neededMB;
    bool res = true;
    res = res
            && UserSettings::get(storage).find(*this,
                    getKey(optSizeXStr, dim, applyCrop), x);
    res = res
            && UserSettings::get(storage).find(*this,
                    getKey(optSizeYStr, dim, applyCrop), y);
    res = res
            && UserSettings::get(storage).find(*this,
                    getKey(optBatchSizeStr, dim, applyCrop), batch);
    res = res
            && UserSettings::get(storage).find(*this,
                    getKey(minMemoryStr, dim, applyCrop), neededMB);
    // check available memory
    res = res && (neededMB <= memoryUtils::MB(gpu.value().lastFreeBytes()));
    if (res) {
        return core::optional<FFTSettings<T>>(
                FFTSettings<T>(x, y, 1, dim.n(), batch, false));
    } else {
        return core::optional<FFTSettings<T>>();
    }
}


template<typename T>
FFTSettings<T> ProgMovieAlignmentCorrelationGPU<T>::runBenchmark(const Dimensions &d,
        size_t extraBytes, bool crop) {
    // FIXME DS remove tmp
    auto tmp1 = FFTSettings<T>(d, d.n(), false);
    FFTSettings<T> tmp(0);
    if (skipAutotuning) {
        tmp = CudaFFT<T>::findMaxBatch(tmp1, gpu.value().lastFreeBytes() - extraBytes);
    } else {
        if (this->verbose) std::cerr << "Benchmarking cuFFT ..." << std::endl;
        // take additional memory requirement into account
        // FIXME DS make sure that result is smaller than available data
        tmp =  CudaFFT<T>::findOptimalSizeOrMaxBatch(gpu.value(), tmp1,
                extraBytes, d.x() == d.y(), crop ? 10 : 20, // allow max 10% change for cropping, 20 for 'padding'
                crop, this->verbose);
    }
    auto goodBatch = tmp.batch();
    if (goodBatch < d.n()) { // in case we cannot process whole batch at once, make reasonable chunks
        goodBatch = d.n() / std::ceil(d.n() / (float)tmp.batch());
    }
    return FFTSettings<T>(tmp.sDim().x(), tmp.sDim().y(), tmp.sDim().z(), tmp.sDim().n(), goodBatch, false);
}

template<typename T>
std::pair<T,T> ProgMovieAlignmentCorrelationGPU<T>::getMovieBorders(
        const AlignmentResult<T> &globAlignment, int verbose) {
    T minX = std::numeric_limits<T>::max();
    T maxX = std::numeric_limits<T>::min();
    T minY = std::numeric_limits<T>::max();
    T maxY = std::numeric_limits<T>::min();
    for (const auto& s : globAlignment.shifts) {
        minX = std::min(std::floor(s.x), minX);
        maxX = std::max(std::ceil(s.x), maxX);
        minY = std::min(std::floor(s.y), minY);
        maxY = std::max(std::ceil(s.y), maxY);
    }
    auto res = std::make_pair(std::abs(maxX - minX), std::abs(maxY - minY));
    if (verbose > 1) {
        std::cout << "Movie borders: x=" << res.first << " y=" << res.second
                << std::endl;
    }
    return res;
}

template<typename T>
LocalAlignmentResult<T> ProgMovieAlignmentCorrelationGPU<T>::computeLocalAlignment(
        const MetaData &movieMD, const Image<T> &dark, const Image<T> &igain,
        const AlignmentResult<T> &globAlignment) {
    using memoryUtils::MB;
    auto movieSettings = this->getMovieSettings(movieMD, false);
    auto patchSettings = this->getPatchSettings(movieSettings);
    this->setNoOfPaches(movieSettings.sDim(), patchSettings.sDim());
    auto correlationSettings = this->getCorrelationSettings(patchSettings);
    auto borders = getMovieBorders(globAlignment, this->verbose > 1);
    auto patchesLocation = this->getPatchesLocation(borders, movieSettings.sDim(),
            patchSettings.sDim());
    T actualScale = correlationSettings.sDim().x() / (T)patchSettings.sDim().x(); // assuming we use square patches

    if (this->verbose) {
        std::cout << "No. of patches: " << this->localAlignPatches.first << " x " << this->localAlignPatches.second << std::endl;
        std::cout << "Actual scale factor (X): " << actualScale << std::endl;
        std::cout << "Settings for the patches: " << patchSettings << std::endl;
        std::cout << "Settings for the correlation: " << correlationSettings << std::endl;
    }
    if (this->localAlignPatches.first <= this->localAlignmentControlPoints.x()
        || this->localAlignPatches.second <= this->localAlignmentControlPoints.y()) {
            throw std::logic_error("More control points than patches. Decrease the number of control points.");
    }

    if ((movieSettings.sDim().x() < patchSettings.sDim().x())
        || (movieSettings.sDim().y() < patchSettings.sDim().y())) {
        REPORT_ERROR(ERR_PARAM_INCORRECT, "Movie is too small for local alignment.");
    }

    // load movie to memory
    if ( ! movie.hasFullMovie()) {
        loadMovie(movieMD, dark, igain);
    }
    // we need to work with full-size movie, with no cropping
    assert(movieSettings.sDim() == movie.getFullDim());

    // prepare filter
    // FIXME DS make sure that the resulting filter is correct, even if we do non-uniform scaling
    MultidimArray<T> filterTmp = this->createLPF(this->getPixelResolution(actualScale), correlationSettings.sDim());
    T* filterData = reinterpret_cast<T*>(BasicMemManager::instance().get(filterTmp.nzyxdim *sizeof(T), MemType::CUDA_MANAGED));
    memcpy(filterData, filterTmp.data, filterTmp.nzyxdim *sizeof(T));
    auto filter = MultidimArray<T>(1, 1, filterTmp.ydim, filterTmp.xdim, filterData);

    // compute max of frames in buffer
    T corrSizeMB = MB<T>((size_t) correlationSettings.fBytesSingle());
    size_t framesInBuffer = std::ceil(MB(gpu.value().lastFreeBytes() / 3) / corrSizeMB);

    // prepare result
    LocalAlignmentResult<T> result { globalHint:globAlignment, movieDim:movieSettings.sDim()};
    result.shifts.reserve(patchesLocation.size() * movieSettings.sDim().n());
    auto refFrame = core::optional<size_t>(globAlignment.refFrame);

    // allocate additional memory for the patches
    // we reuse the data, so we need enough space for the patches data
    // and for the resulting correlations, which cannot be bigger than (padded) input data
    size_t bytes = std::max(patchSettings.fBytes(), patchSettings.sBytes());

    auto createContext = [&, this](auto &p) {
        static std::mutex mutex;
        std::unique_lock<std::mutex> lock(mutex); // we need to lock this part to ensure serial access to result.shifts
        auto context = PatchContext(result);
        context.verbose = this->verbose;
        context.maxShift = this->maxShift;
        context.shiftsOffset = result.shifts.size();
        context.N = patchSettings.sDim().n();
        context.scale = std::make_pair(patchSettings.sDim().x() / (T) correlationSettings.sDim().x(),
            patchSettings.sDim().y() / (T) correlationSettings.sDim().y());
        context.refFrame = refFrame;
        context.centerSize = getCenterSize(this->maxShift);
        context.framesInCorrelationBuffer = framesInBuffer;
        // prefill some info about patch
        for (size_t i = 0;i < movieSettings.sDim().n();++i) {
            FramePatchMeta<T> tmp = p;
            // keep consistent with data loading
            int globShiftX = std::round(globAlignment.shifts.at(i).x);
            int globShiftY = std::round(globAlignment.shifts.at(i).y);
            tmp.id_t = i;
            // total shift (i.e. global shift + local shift) will be computed later on
            result.shifts.emplace_back(tmp, Point2D<T>(globShiftX, globShiftY));
        }
        return context;
    };

    std::vector<T*> corrBuffers(loadPool.size()); // initializes to nullptrs
    std::vector<T*> patchData(loadPool.size()); // initializes to nullptrs
    std::vector<std::future<void>> futures;
    futures.reserve(patchesLocation.size());

    // use additional thread that would load the data at the background
    // get alignment for all patches and resulting correlations
    for (auto &&p : patchesLocation) {
        auto routine = [&](int thrId) {
            if (this->verbose > 1) {
                std::cout << "\nQueuing patch " << p.id_x << " " << p.id_y << " for processing\n";
            }
            auto context = createContext(p);

            // alllocate and clear patch data
            if (nullptr == patchData.at(thrId)) {
                patchData[thrId] = reinterpret_cast<T*>(BasicMemManager::instance().get(bytes, MemType::CUDA_HOST));
            }
            auto *data = patchData.at(thrId);
            memset(data, 0, bytes);

            // allocate and clear correlation data
            if (nullptr == corrBuffers.at(thrId)) {
                corrBuffers[thrId] = reinterpret_cast<T*>(BasicMemManager::instance().get(context.corrElems() * sizeof(T), MemType::CUDA_HOST));
            }
            auto *correlations = corrBuffers.at(thrId);
            memset(correlations, 0, context.corrElems() * sizeof(T));

            // get data
            getPatchData(p.rec, globAlignment, data);

            // convert to FFT, downscale them and compute correlations
            GPUPool.push([&](int){
                performFFTAndScale<T>(data, patchSettings.sDim().n(), patchSettings.sDim().x(), patchSettings.sDim().y(), patchSettings.batch(),
                        correlationSettings.fDim().x(), correlationSettings.sDim().y(), filter);
                computeCorrelations(context.centerSize, context.N, (std::complex<T>*)data, correlationSettings.fDim().x(),
                        correlationSettings.sDim().x(),
                        correlationSettings.fDim().y(), context.framesInCorrelationBuffer,
                        correlationSettings.batch(), correlations);
            }).get(); // wait till done - i.e. correlations are computed and on CPU

            // compute resulting shifts
            computeShifts(correlations, context);
        };
        futures.emplace_back(loadPool.push(routine));
    }
    // wait for the last processing thread
    for (auto &f : futures) { f.get(); }

    for (auto *ptr : corrBuffers) { BasicMemManager::instance().give(ptr); }
    for (auto *ptr : patchData) { BasicMemManager::instance().give(ptr); }
    BasicMemManager::instance().give(filterData);

    auto coeffs = BSplineHelper::computeBSplineCoeffs(movieSettings.sDim(), result,
            this->localAlignmentControlPoints, this->localAlignPatches,
            this->verbose, this->solverIterations);
    result.bsplineRep = core::optional<BSplineGrid<T>>(
            BSplineGrid<T>(this->localAlignmentControlPoints, coeffs.first, coeffs.second));

    return result;
}

template<typename T>
LocalAlignmentResult<T> ProgMovieAlignmentCorrelationGPU<T>::localFromGlobal(
        const MetaData& movie,
        const AlignmentResult<T> &globAlignment) {
    auto movieSettings = getMovieSettings(movie, false);
    LocalAlignmentResult<T> result { globalHint:globAlignment, movieDim:movieSettings.sDim() };
    auto patchSettings = this->getPatchSettings(movieSettings);
    this->setNoOfPaches(movieSettings.sDim(), patchSettings.sDim());
    auto borders = getMovieBorders(globAlignment, 0);
    auto patchesLocation = this->getPatchesLocation(borders, movieSettings.sDim(),
            patchSettings.sDim());
    // get alignment for all patches
    for (auto &&p : patchesLocation) {
        // process it
        for (size_t i = 0; i < movieSettings.sDim().n(); ++i) {
            FramePatchMeta<T> tmp = p;
            tmp.id_t = i;
            result.shifts.emplace_back(tmp, Point2D<T>(globAlignment.shifts.at(i).x, globAlignment.shifts.at(i).y));
        }
    }

    auto coeffs = BSplineHelper::computeBSplineCoeffs(movieSettings.sDim(), result,
            this->localAlignmentControlPoints, this->localAlignPatches,
            this->verbose, this->solverIterations);
    result.bsplineRep = core::optional<BSplineGrid<T>>(
            BSplineGrid<T>(this->localAlignmentControlPoints, coeffs.first, coeffs.second));

    return result;
}

template<typename T>
void ProgMovieAlignmentCorrelationGPU<T>::applyShiftsComputeAverage(
        const MetaData& movie, const Image<T>& dark, const Image<T>& igain,
        Image<T>& initialMic, size_t& Ninitial, Image<T>& averageMicrograph,
        size_t& N, const AlignmentResult<T> &globAlignment) {
    applyShiftsComputeAverage(movie, dark, igain, initialMic, Ninitial, averageMicrograph,
            N, localFromGlobal(movie, globAlignment));
}

template <typename T>
auto ProgMovieAlignmentCorrelationGPU<T>::getOutputStreamCount()
{
    gpu.value().updateMemoryInfo();
    auto maxStreams = [this]()
    {
        auto count = 4;
        // upper estimation is 2 full frames of GPU data per stream
        while (2 * count * movie.getFullDim().xy() * sizeof(T) > this->gpu.value().lastFreeBytes())
        {
            count--;
        }
        return std::max(count, 1);
    }();
    if (this->verbose > 1)
    {
        std::cout << "GPU streams used for output generation: " << maxStreams << "\n";
    }
    return maxStreams;
}

template<typename T>
void ProgMovieAlignmentCorrelationGPU<T>::applyShiftsComputeAverage(
        const MetaData& movieMD, const Image<T>& dark, const Image<T>& igain,
        Image<T>& initialMic, size_t& Ninitial, Image<T>& averageMicrograph,
        size_t& N, const LocalAlignmentResult<T> &alignment) {
    Ninitial = N = 0;
    if ( ! alignment.bsplineRep) {
        REPORT_ERROR(ERR_VALUE_INCORRECT,
            "Missing BSpline representation. This should not happen. Please contact developers.");
    }

    struct AuxData {
        MultidimArray<T> shiftedFrame;
        MultidimArray<T> reducedFrame;
        GeoTransformer<T> transformer;
        MultidimArray<double> croppedFrameD;
        MultidimArray<double> reducedFrameD;
        GPU stream;
        T *hIn;
        T *hOut;
    };

    auto coeffs = std::make_pair(alignment.bsplineRep.value().getCoeffsX(),
        alignment.bsplineRep.value().getCoeffsY());

    // prepare data for each thread
    ctpl::thread_pool pool = ctpl::thread_pool(getOutputStreamCount());
    auto aux = std::vector<AuxData>(pool.size());
    auto futures = std::vector<std::future<void>>();
    for (auto i = 0; i < pool.size(); ++i) {
        aux[i].stream = GPU(gpu.value().device(), i + 1);
        aux[i].hIn = reinterpret_cast<T*>(BasicMemManager::instance().get(movie.getFullDim().xy() * sizeof(T), MemType::CUDA_HOST));
    }

    const T binning = this->getOutputBinning();
    int frameIndex = -1;
    std::mutex mutex;
    FOR_ALL_OBJECTS_IN_METADATA(movieMD)
    {
        frameIndex++;
        if ((frameIndex >= this->nfirstSum) && (frameIndex <= this->nlastSum)) {
            // user might want to align frames 3..10, but sum only 4..6
            // by deducting the first frame that was aligned, we get proper offset to the stored memory
            int frameOffset = frameIndex - this->nfirst;
            auto routine = [&](int threadId) {
                auto &a = aux[threadId];
                a.stream.set();
                auto *data = movie.getFullFrame(frameIndex).data;
                auto croppedFrame = MultidimArray(1, 1, movie.getFullDim().y(), movie.getFullDim().x(), a.hIn);
                memcpy(croppedFrame.data, data, croppedFrame.yxdim * sizeof(T));

                if (binning > 0) {
                    typeCast(croppedFrame, a.croppedFrameD);
                    auto scale = [binning](auto dim) {
                    return static_cast<int>(
                        std::floor(static_cast<T>(dim) / binning));
                    };
                    scaleToSizeFourier(1, scale(croppedFrame.ydim),
                                    scale(croppedFrame.xdim),
                                    a.croppedFrameD, a.reducedFrameD);

                    typeCast(a.reducedFrameD, a.reducedFrame);
                    // we need to construct cropped frame again with reduced size, but with the original memory block
                    croppedFrame = MultidimArray(1, 1, a.reducedFrame.ydim, a.reducedFrame.xdim, a.hIn);
                    memcpy(croppedFrame.data, a.reducedFrame.data, a.reducedFrame.yxdim * sizeof(T));
                }

                if ( ! this->fnInitialAvg.isEmpty()) {
                    std::unique_lock<std::mutex> lock(mutex);
                    if (0 == initialMic().yxdim)
                        initialMic() = croppedFrame;
                    else
                        initialMic() += croppedFrame;
                    Ninitial++;
                }

                if ( ! this->fnAligned.isEmpty() || ! this->fnAvg.isEmpty()) {
                    if (nullptr == a.hOut) {
                        a.hOut = reinterpret_cast<T*>(BasicMemManager::instance().get(croppedFrame.yxdim * sizeof(T), MemType::CUDA_HOST));
                    }
                    auto shiftedFrame = MultidimArray<T>(1, 1, croppedFrame.ydim, croppedFrame.xdim, a.hOut);
                    a.transformer.initLazyForBSpline(croppedFrame.xdim, croppedFrame.ydim, alignment.movieDim.n(),
                            this->localAlignmentControlPoints.x(), this->localAlignmentControlPoints.y(), this->localAlignmentControlPoints.n(), a.stream);
                    a.transformer.applyBSplineTransform(this->BsplineOrder, shiftedFrame, croppedFrame, coeffs, frameOffset);

                    a.stream.synch(); // make sure that data is fetched from GPU
                    if (this->fnAligned != "") {
                        Image<T> tmp(shiftedFrame);
                        std::unique_lock<std::mutex> lock(mutex);
                        tmp.write(this->fnAligned, frameOffset + 1, true,
                                WRITE_REPLACE);
                    }
                    if (this->fnAvg != "") {
                        std::unique_lock<std::mutex> lock(mutex);
                        if (0 == averageMicrograph().yxdim)
                            averageMicrograph() = shiftedFrame;
                        else
                            averageMicrograph() += shiftedFrame;
                        N++;
                    }
                }
            };
            futures.emplace_back(pool.push(routine));
        }
    }
    for (auto &t : futures) {
        t.get();
    }
    for (auto i = 0; i < pool.size(); ++i) {
        BasicMemManager::instance().give(aux[i].hIn);
        BasicMemManager::instance().give(aux[i].hOut);
    }
}

template<typename T>
auto ProgMovieAlignmentCorrelationGPU<T>::GlobalAlignmentHelper::findBatchThreadsForScale(const Dimensions &movie, const Dimensions &correlation, const GPU &gpu) {
    auto mSize = findGoodCropSize(movie, gpu);
    std::cout << "Good movie size: " << mSize << "\n";
    auto cSize = findGoodCorrelationSize(correlation, gpu);
    std::cout << "Good correlation size: " << cSize << "\n";
using memoryUtils::MB;
    const auto maxBytes = gpu.lastFreeBytes() * 0.9f; // leave some buffer in case of memory fragmentation
    auto getMemReq = [&mSize, &cSize](size_t batch, size_t streams) {
        auto in = FFTSettings<T>(mSize.x(), mSize.y(), 1, mSize.n(), batch);
        auto out = FFTSettings<T>(cSize.x(), cSize.y(), 1, cSize.n(), batch);
        size_t planSize = CudaFFT<T>().estimatePlanBytes(in);
        size_t aux = std::max(in.sBytesBatch(), out.fBytesBatch());
        size_t fd = in.fBytesBatch();
        return (planSize + aux + fd) * streams;
    };

    auto cond = [&mSize](size_t batch, size_t threads) {
        // we want only no. of batches that can process the movie without extra invocations
        return (0 == mSize.n() % batch) && (threads * batch) <= mSize.n();
    };

    auto set = [&mSize, &cSize, this](size_t batch, size_t streams, size_t threads) {
        movieSettings = FFTSettings<T>(mSize, batch);
        correlationSettings = FFTSettings<T>(cSize);
        gpuStreams = streams;
        cpuThreads = threads;
    };

    if ((getMemReq(1, 2) <= maxBytes) && cond(1, 2)){
        // more streams do not make sense because we're limited by the transfers
        // bigger batch leads to more time wasted on memory allocation - it gets importand if you have lower number of frames
        set(1, 2, 4); // two streams to overlap memory transfers and computations, 4 threads to make sure they are fully utilized
    } else {
        set(1, 1, 2);
    }
    
    std::cout << "using " << cpuThreads << " threads, " << gpuStreams << " streams and batch of " << movieSettings.batch() << "\n";
}


template<typename T>
AlignmentResult<T> ProgMovieAlignmentCorrelationGPU<T>::computeGlobalAlignment(
        const MetaData &movieMD, const Image<T> &dark, const Image<T> &igain) {
    
    using memoryUtils::MB;
    const auto movieSize = this->getMovieSize();
    GlobalAlignmentHelper helper;
    helper.findBatchThreadsForScale(movieSize, this->getCorrelationHint(movieSize), gpu.value());

    auto &movieSettings = helper.movieSettings;
    auto &correlationSettings = helper.correlationSettings;
    T actualScale = correlationSettings.sDim().x() / (T)movieSettings.sDim().x();

    // prepare filter
    MultidimArray<T> filterTmp = this->createLPF(this->getPixelResolution(actualScale), correlationSettings.sDim());
    T* filterData = reinterpret_cast<T*>(BasicMemManager::instance().get(filterTmp.nzyxdim *sizeof(T), MemType::CUDA_MANAGED));
    memcpy(filterData, filterTmp.data, filterTmp.nzyxdim *sizeof(T));
    auto filter = MultidimArray<T>(1, 1, filterTmp.ydim, filterTmp.xdim, filterData);
    
    if (this->verbose) {
        std::cout << "Requested scale factor: " << this->getScaleFactor() << std::endl;
        std::cout << "Actual scale factor (X): " << actualScale << std::endl;
        std::cout << "Settings for the movie: " << movieSettings << std::endl;
        std::cout << "Settings for the correlation: " << correlationSettings << std::endl;
    }

    const bool loadMovie = !movie.hasFullMovie();
    if (loadMovie) {
        movie.setFullDim(movieSize); // this will also reserve enough space in the movie vector
    }
    // create a buffer for correlations in FD
    auto *scaledFrames = reinterpret_cast<std::complex<T>*>(BasicMemManager::instance().get(correlationSettings.fBytes(), MemType::CPU_PAGE_ALIGNED));

    auto cpuPool = ctpl::thread_pool(helper.cpuThreads);
    auto gpuPool = ctpl::thread_pool(helper.gpuStreams);
    std::vector<T*> croppedFrames(cpuPool.size());
    std::vector<GlobAlignmentData<T>> auxData(gpuPool.size());

    std::vector<GPU> streams(gpuPool.size());
    for (auto i = 0; i < streams.size(); ++i) {
        streams.at(i) = GPU(gpu.value().device(), i + 1);
        auto routine = [&movieSettings, &correlationSettings, &auxData, &streams, i](int stream) {
            streams.at(i).set();
            auxData.at(i).alloc(movieSettings.createBatch(), correlationSettings, streams.at(i));
        };
        gpuPool.push(routine);
    }

    for (auto i = 0; i < movieSettings.sDim().n(); i += movieSettings.batch()) {
        auto routine = [&](int thrId, size_t first, size_t count) {
            if (loadMovie) {
                loadFrames(movieMD, dark, igain, first, count);
            }
            if (nullptr == croppedFrames[thrId])
            {
                croppedFrames[thrId] = reinterpret_cast<T *>(BasicMemManager::instance().get(movieSettings.sBytesBatch(), MemType::CUDA_HOST));
                // reinterpret_cast<T *>(cudaHostAlloc(&cFrames, movieSettings.sBytesBatch(), cudaHostAllocDefault));
                // croppedFrames[thrId] = cFrames;
                // croppedFrames[thrId] = cFrames = reinterpret_cast<T *>(BasicMemManager::instance().get(movieSettings.sBytesBatch(), MemType::CPU_PAGE_ALIGNED));
                // GPU::pinMemory(cFrames, movieSettings.sBytesBatch());
            }
            auto *cFrames = croppedFrames[thrId];
            getCroppedFrames(movieSettings, cFrames, first, count); 
            gpuPool.push([&](int stream){performFFTAndScale(croppedFrames[thrId], movieSettings.createBatch(),
                                                scaledFrames + first * correlationSettings.fDim().sizeSingle(), correlationSettings,
                                                filter, streams[stream], auxData[stream]);}).get();
        };
        cpuPool.push(routine, i, movieSettings.batch());
    }
    cpuPool.stop(true);
    gpuPool.stop(true);
    for (auto *ptr : croppedFrames) {
        BasicMemManager::instance().give(ptr);
    }
    for (auto &d : auxData) {
        d.release();
    }
    BasicMemManager::instance().release();

    exit(0);


    T corrSizeMB = ((size_t) correlationSettings.fDim().xy()
            * sizeof(std::complex<T>)) / ((T) 1024 * 1024);
    size_t framesInBuffer = std::ceil((MB(gpu.value().lastFreeBytes() / 3)) / corrSizeMB);

    auto reference = core::optional<size_t>();
    // // load movie to memory
    // if ( ! movie.hasFullMovie()) {
    //     loadMovie(movieMD, dark, igain);
    // }

    
    // {
    //     T *croppedFrames1 = nullptr;
    //     T *croppedFrames2 = nullptr;

    //     std::future<void> prevTask;
    //     ctpl::thread_pool pool = ctpl::thread_pool(1);

    //     for (auto i = 0; i < movieSettings.sDim().n(); i += movieSettings.batch())
    //     {
    //         auto noOfFrames = std::min(movieSettings.batch(), movieSettings.sDim().n() - i);
    //         if (nullptr == croppedFrames1)
    //         {
    //             croppedFrames1 = reinterpret_cast<T *>(BasicMemManager::instance().get(movieSettings.sBytesBatch(), MemType::CUDA_HOST));
    //         }

    //         getCroppedFrames(movieSettings, croppedFrames1, i, noOfFrames);

    //         if (prevTask.valid())
    //         {
    //             gpu.value().synch();
    //             prevTask.get();
    //         }
    //         std::swap(croppedFrames1, croppedFrames2);
    //         auto task = [&](int)
    //         {
    //             performFFTAndScale(croppedFrames2, movieSettings.createSubset(noOfFrames),
    //                                scaledFrames + i * correlationSettings.fDim().sizeSingle(), correlationSettings.createSubset(noOfFrames),
    //                                filter, gpu.value());
    //         };
    //         prevTask = pool.push(task);
    //     };
    //     prevTask.get();
    //     BasicMemManager::instance().give(croppedFrames1);
    //     BasicMemManager::instance().give(croppedFrames2);
    // }

    auto scale = std::make_pair(movieSettings.sDim().x() / (T) correlationSettings.sDim().x(),
        movieSettings.sDim().y() / (T) correlationSettings.sDim().y());

    auto result = computeShifts(this->verbose, this->maxShift, scaledFrames, correlationSettings.copyForBatch(this->getCorrelationSettings(movieSettings).batch()),
        movieSettings.sDim().n(),
        scale, framesInBuffer, reference);


    // in two / multiple threads crop frames and threadsprocess each buffer separately on gpu thread


// // FIXME DS in case of big movies (EMPIAR 10337), we have to optimize the memory management
// // also, when autotuning is off, we don't need to create the copy at all
// size_t bytes = std::max(movieSettings.sBytes(), movieSettings.fBytes());
// auto *data = reinterpret_cast<T*>(BasicMemManager::instance().get(bytes, MemType::CPU_PAGE_ALIGNED));
// // GPU::pinMemory(data, elems * sizeof(T));
// getCroppedMovie(movieSettings, data);

// auto result = align(data, movieSettings, correlationSetting,
//                 filter, reference,
//         this->maxShift, framesInBuffer, this->verbose);
// GPU::unpinMemory(data);

// BasicMemManager::instance().give(data);


    BasicMemManager::instance().give(filterData);
    BasicMemManager::instance().give(scaledFrames);
    BasicMemManager::instance().release(MemType::CUDA);
    return result;
}

template<typename T>
AlignmentResult<T> ProgMovieAlignmentCorrelationGPU<T>::align(T *data,
        const FFTSettings<T> &in, const FFTSettings<T> &correlation,
        MultidimArray<T> &filter,
        core::optional<size_t> &refFrame,
        size_t maxShift, size_t framesInCorrelationBuffer, int verbose) {
    assert(nullptr != data);
    size_t N = in.sDim().n();
    // scale and transform to FFT on GPU
    performFFTAndScale<T>(data, N, in.sDim().x(), in.sDim().y(), in.batch(),
            correlation.fDim().x(), correlation.fDim().y(), filter);

    auto scale = std::make_pair(in.sDim().x() / (T) correlation.sDim().x(),
            in.sDim().y() / (T) correlation.sDim().y());

    return computeShifts(verbose, maxShift, (std::complex<T>*) data, correlation,
            in.sDim().n(),
            scale, framesInCorrelationBuffer, refFrame);
}

// template<typename T>
// void ProgMovieAlignmentCorrelationGPU<T>::getCroppedMovie(const FFTSettings<T> &settings,
//         T *output) {
//     for (size_t n = 0; n < settings.sDim().n(); ++n) {
//         T *src = movie.getFullFrame(n).data; // points to first float in the image
//         T *dest = output + (n * settings.sDim().xy()); // points to first float in the image
//         for (size_t y = 0; y < settings.sDim().y(); ++y) {
//             memcpy(dest + (settings.sDim().x() * y),
//                     src + (movie.getFullDim().x() * y),
//                     settings.sDim().x() * sizeof(T));
//         }
//     }
// }

template<typename T>
void __attribute__((optimize("O3"))) ProgMovieAlignmentCorrelationGPU<T>::getCroppedFrames(const FFTSettings<T> &settings,
        T *output, size_t firstFrame, size_t noOfFrames) {
    for (size_t n = 0; n < noOfFrames; ++n) {
        T *src = movie.getFullFrame(n + firstFrame).data; // points to first float in the image
        T *dest = output + (n * settings.sDim().xy()); // points to first float in the image
        for (size_t y = 0; y < settings.sDim().y(); ++y) {
            memcpy(dest + (settings.sDim().x() * y),
                    src + (movie.getFullDim().x() * y),
                    settings.sDim().x() * sizeof(T));
        }
    }
}

template<typename T>
MultidimArray<T> &ProgMovieAlignmentCorrelationGPU<T>::Movie::allocate(size_t x, size_t y) {
    auto *ptr = memoryUtils::page_aligned_alloc<T>(x * y, false);
    return mFullFrames.emplace_back(1, 1, y, x, ptr);
}

template<typename T>
void ProgMovieAlignmentCorrelationGPU<T>::Movie::releaseFullFrames() {
    for (auto &f : mFullFrames) {
        BasicMemManager::instance().give(f.data);
        f.data = nullptr;
    }
}

template<typename T>
void ProgMovieAlignmentCorrelationGPU<T>::loadMovie(const MetaData& movieMD,
        const Image<T>& dark, const Image<T>& igain) {
    movie.setFullDim(this->getMovieSize()); // this will also reserve enough space in the movie vector
    auto &movieDim = movie.getFullDim();

    ctpl::thread_pool pool = ctpl::thread_pool(2);
    auto futures = std::vector<std::future<void>>();

    int movieImgIndex = -1;
    for (size_t objId : movieMD.ids())
    {
        // update variables
        movieImgIndex++;
        if (movieImgIndex < this->nfirst) continue;
        if (movieImgIndex > this->nlast) break;

        // load image
        auto &dest = movie.allocate(movieDim.x(), movieDim.y());
        auto routine = [&dest, &movieMD, &dark, &igain, objId, this](int) {
            Image<T> frame(dest);
            this->loadFrame(movieMD, dark, igain, objId, frame);
        };
        futures.emplace_back(pool.push(routine));
    }
    for (auto &f : futures) {
        f.get();
    }
}

template<typename T>
void ProgMovieAlignmentCorrelationGPU<T>::loadFrames(const MetaData& movieMD,
        const Image<T>& dark, const Image<T>& igain, size_t first, size_t count) {
    auto &movieDim = movie.getFullDim();
    int frameIndex = -1;
    size_t counter = 0;
    for (size_t objId : movieMD.ids())
    {
        // get to correct index
        frameIndex++;
        if (frameIndex < this->nfirst) continue;
        if (frameIndex > this->nlast) break;

        if ((counter >= first) && counter < (first + count)) {
            // load image
            auto *ptr = reinterpret_cast<T*>(BasicMemManager::instance().get(movieDim.xy() * sizeof(T), MemType::CPU_PAGE_ALIGNED));
            auto &dest = movie.getFullFrame(frameIndex);
            dest.data = ptr;
            Image<T> frame(dest);
            this->loadFrame(movieMD, dark, igain, objId, frame);
        }
        counter++;
    }
}

template <typename T>
auto ProgMovieAlignmentCorrelationGPU<T>::computeShifts(
    T *correlations,
    PatchContext context)
{ // pass by copy, this will be run asynchronously)
    // N is number of images, n is number of correlations
    // compute correlations (each frame with following ones)

    // result is a centered correlation function with (hopefully) a cross
    // indicating the requested shift

    // auto routine = [this](int, PatchContext context, T* correlations) {
        auto noOfCorrelations = context.N * (context.N - 1) / 2;
        // we are done with the input data, so release it
        Matrix2D<T> A(noOfCorrelations, context.N - 1);
        Matrix1D<T> bX(noOfCorrelations), bY(noOfCorrelations);

        // find the actual shift (max peak) for each pair of frames
        // and create a set or equations
        size_t idx = 0;

        for (size_t i = 0; i < context.N - 1; ++i) {
            for (size_t j = i + 1; j < context.N; ++j) {
                size_t offset = idx * context.centerSize * context.centerSize;
                MultidimArray<T> Mcorr(1, 1, context.centerSize, context.centerSize, correlations + offset);
                Mcorr.setXmippOrigin();
                bestShift(Mcorr, bX(idx), bY(idx), NULL,
                        context.maxShift / context.scale.first);
                bX(idx) *= context.scale.first; // scale to expected size
                bY(idx) *= context.scale.second;
                if (context.verbose > 1) {
                    std::cerr << "Frame " << i << " to Frame " << j << " -> ("
                            << bX(idx) << "," << bY(idx) << ")" << std::endl;
                }
                for (int ij = i; ij < j; ij++) {
                    A(idx, ij) = 1;
                }
                idx++;
            }
        }

        // auto LES = [bX, bY, context, A, this](int) mutable {
            // now get the estimated shift (from the equation system)
            // from each frame to successing frame
            auto result = this->computeAlignment(bX, bY, A, context.refFrame, context.N, context.verbose);
            // prefill some info about patch
            for (size_t i = 0;i < context.N;++i) {
                // update total shift (i.e. global shift + local shift)
                context.result.shifts[i + context.shiftsOffset].second += result.shifts[i];
            }
        // };
    // };
}

template<typename T>
AlignmentResult<T> ProgMovieAlignmentCorrelationGPU<T>::computeShifts(int verbose,
        size_t maxShift,
        std::complex<T>* data, const FFTSettings<T>& settings, size_t N,
        std::pair<T, T>& scale,
        size_t framesInCorrelationBuffer,
        const core::optional<size_t>& refFrame) {
    // N is number of images, n is number of correlations
    // compute correlations (each frame with following ones)
    size_t centerSize = getCenterSize(maxShift);
    auto *correlations = new T[N*(N-1)/2 * centerSize * centerSize]();
    computeCorrelations(centerSize, N, data, settings.fDim().x(),
            settings.sDim().x(),
            settings.fDim().y(), framesInCorrelationBuffer,
            settings.batch(), correlations);
    // result is a centered correlation function with (hopefully) a cross
    // indicating the requested shift

    // we are done with the input data, so release it
    Matrix2D<T> A(N * (N - 1) / 2, N - 1);
    Matrix1D<T> bX(N * (N - 1) / 2), bY(N * (N - 1) / 2);

    // find the actual shift (max peak) for each pair of frames
    // and create a set or equations
    size_t idx = 0;
    MultidimArray<T> Mcorr(centerSize, centerSize);
    T* origData = Mcorr.data;

    for (size_t i = 0; i < N - 1; ++i) {
        for (size_t j = i + 1; j < N; ++j) {
            size_t offset = idx * centerSize * centerSize;
            Mcorr.data = correlations + offset;
            Mcorr.setXmippOrigin();
            bestShift(Mcorr, bX(idx), bY(idx), NULL,
                    maxShift / scale.first);
            bX(idx) *= scale.first; // scale to expected size
            bY(idx) *= scale.second;
            if (verbose > 1) {
                std::cerr << "Frame " << i << " to Frame " << j << " -> ("
                        << bX(idx) << "," << bY(idx) << ")" << std::endl;
            }
            for (int ij = i; ij < j; ij++) {
                A(idx, ij) = 1;
            }
            idx++;
        }
    }
    Mcorr.data = origData;
    delete[] correlations;

    // now get the estimated shift (from the equation system)
    // from each frame to successing frame
    AlignmentResult<T> result = this->computeAlignment(bX, bY, A, refFrame, N, verbose);
    return result;
}

template<typename T>
size_t ProgMovieAlignmentCorrelationGPU<T>::getMaxFilterBytes(
        const Dimensions &dim) {
    size_t maxXPow2 = std::ceil(log(dim.x()) / log(2));
    size_t maxX = std::pow(2, maxXPow2);
    size_t maxFFTX = maxX / 2 + 1;
    size_t maxYPow2 = std::ceil(log(dim.y()) / log(2));
    size_t maxY = std::pow(2, maxYPow2);
    size_t bytes = maxFFTX * maxY * sizeof(T);
    return bytes;
}

template<typename T>
void ProgMovieAlignmentCorrelationGPU<T>::releaseAll()
{
    BasicMemManager::instance().release();
    movie.releaseFullFrames();
};

// explicit specialization
template class ProgMovieAlignmentCorrelationGPU<float> ;
