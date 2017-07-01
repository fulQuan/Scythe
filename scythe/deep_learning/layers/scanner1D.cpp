/**
    scanner1D.cpp
    Multi-grained scanning

    @author Antoine Passemiers
    @version 1.0 10/06/2017
*/

#include "scanner1D.hpp"


ScannedDataset1D::ScannedDataset1D(
    data_t* data, size_t N, size_t M, size_t kc, int dtype) : 
    N(N),                // Number of instances
    M(M),                // Instance height
    kc(kc),              // Kernel width
    sc(M - kc + 1),      // Number of kernel positions per column
    Nprime(N * sc),      // Number of instances after scanning
    Mprime(kc),          // Number of features after scanning
    data(data),
    dtype(dtype) {}

data_t ScannedDataset1D::operator()(size_t i, size_t j) {
    return 0; // TODO
}

std::shared_ptr<void> ScannedDataset1D::_operator_ev(const size_t j) {
    return nullptr; // TODO
}

size_t ScannedDataset1D::getSc() {
    return this->sc;
}

size_t ScannedDataset1D::getNumInstances() {
    return this->Nprime;
}

size_t ScannedDataset1D::getNumFeatures() {
    return this->Mprime;
}

size_t ScannedDataset1D::getRequiredMemorySize() {
    return this->Nprime * this->Mprime;
}

size_t ScannedDataset1D::getNumVirtualInstancesPerInstance() {
    return sc;
}

ScannedTargets1D::ScannedTargets1D(data_t* data, size_t n_instances, size_t sc) :
    data(data), n_rows(n_instances), s(sc) {}

ScannedTargets1D::ScannedTargets1D(const ScannedTargets1D& other) :
    data(other.data), n_rows(other.n_rows), s(other.s) {}

ScannedTargets1D& ScannedTargets1D::operator=(const ScannedTargets1D& other) {
    this->data = data;
    this->n_rows = n_rows;
    this->s = s;
    return *this;
}

data_t ScannedTargets1D::operator[](const size_t i) {
    return data[i / s];
}

MultiGrainedScanner1D::MultiGrainedScanner1D(LayerConfig lconfig, size_t kc) : 
    Layer(lconfig), kc(kc) {
}

vdataset_p MultiGrainedScanner1D::virtualize(MDDataset dataset) {
    assert(dataset.n_dims == 2);
    assert(dataset.dims[0] > 0);
    assert(dataset.dims[1] > 0);
    Layer::vdataset = std::shared_ptr<ScannedDataset1D>(
        new ScannedDataset1D(
            dataset.data,
            dataset.dims[0],
            dataset.dims[1],
            this->kc,
            dataset.dtype));
    return Layer::vdataset;
}

vtargets_p MultiGrainedScanner1D::virtualizeTargets(Labels<target_t>* targets) {
    ScannedDataset1D* vdataset = dynamic_cast<ScannedDataset1D*>((this->vdataset).get());
    size_t sc = vdataset->getSc();
    size_t n_rows = vdataset->getNumInstances();
    assert(sc > 0);
    return std::shared_ptr<ScannedTargets1D>(new ScannedTargets1D(targets->data, n_rows, sc));
}

size_t MultiGrainedScanner1D::getRequiredMemorySize() {
    size_t memory_size = vdataset->getNumInstances();
    assert(memory_size > 0);
    if (lconfig.fconfig.task == gbdf::CLASSIFICATION_TASK) {
        memory_size *= lconfig.fconfig.n_classes;
    }
    return memory_size * lconfig.n_forests;
}

size_t MultiGrainedScanner1D::getNumVirtualFeatures() {
    size_t n_vfeatures = dynamic_cast<ScannedDataset1D*>(vdataset.get())->getSc();
    assert(n_vfeatures > 0);
    if (lconfig.fconfig.task == gbdf::CLASSIFICATION_TASK) {
        n_vfeatures *= lconfig.fconfig.n_classes;
    }
    return n_vfeatures * lconfig.n_forests;
}