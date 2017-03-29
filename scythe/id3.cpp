#include "id3.hpp"

inline size_t sum_counts(size_t* counters, size_t n_counters) {
    size_t total = 0;
    for (uint i = 0; i < n_counters; i++) {
        total += counters[i];
    }
    return total;
}

Node* newNode(size_t n_classes) {
    Node* node = static_cast<Node*>(malloc(sizeof(Node)));
    node->feature_id = NO_FEATURE;
    node->counters = static_cast<size_t*>(malloc(n_classes * sizeof(size_t)));
    node->n_instances = NO_INSTANCE;
    node->score = INFINITY;
    node->split_value = NO_SPLIT_VALUE;
    node->left_child = nullptr;
    node->right_child = nullptr;
    return node;
}

Density* computeDensities(data_t* data, size_t n_instances, size_t n_features,
                                 size_t n_classes, data_t nan_value) {
    size_t s = sizeof(data_t);
    Density* densities = static_cast<Density*>(malloc(n_features * sizeof(Density)));
    data_t* sorted_values = static_cast<data_t*>(malloc(n_instances * s));
    for (uint f = 0; f < n_features; f++) {
        densities[f].quartiles = static_cast<data_t*>(malloc(4 * s));
        densities[f].deciles = static_cast<data_t*>(malloc(10 * s));
        densities[f].percentiles = static_cast<data_t*>(malloc(100 * s));
        densities[f].counters_left = static_cast<size_t*>(malloc(n_classes * sizeof(size_t)));
        densities[f].counters_right = static_cast<size_t*>(malloc(n_classes * sizeof(size_t)));
        densities[f].counters_nan = static_cast<size_t*>(malloc(n_classes * sizeof(size_t)));
        // Putting nan values aside
        bool is_categorical = true;
        size_t n_acceptable = 0;
        data_t data_point;
        for (uint i = 0; i < n_instances; i++) {
            data_point = data[i * n_features + f];
            if (data_point != nan_value) {
                sorted_values[n_acceptable] = data_point;
                n_acceptable++;
                if (is_categorical && !(round(data_point) == data_point)) {
                    is_categorical = false;
                }
            }
        }
        densities[f].is_categorical = is_categorical;
        // Sorting acceptable values
        size_t k;
        data_t x;
        for (uint i = 0; i < n_acceptable; i++) {
            x = sorted_values[i];
            k = i;
            while (k > 0 && sorted_values[k - 1] > x) {
                sorted_values[k] = sorted_values[k - 1];
                k--;
            }
            sorted_values[k] = x;
        }
        // Computing quartiles, deciles, percentiles
        float step_size = static_cast<float>(n_acceptable) / 100.0f;
        float current_index = 0.0;
        int rounded_index = 0;
        for (uint i = 0; i < 10; i++) {
            densities[f].deciles[i] = sorted_values[rounded_index];
            for (uint k = 0; k < 10; k++) {
                rounded_index = static_cast<int>(floor(current_index));
                densities[f].percentiles[10 * i + k] = sorted_values[rounded_index];
                current_index += step_size;
            }
        }
    }
    free(sorted_values);
    return densities;
}

inline float ShannonEntropy(float probability) {
    return -probability * log2(probability);
}

inline float GiniCoefficient(float probability) {
    return 1.f - probability * probability;
}

inline double getFeatureCost(Density* density, size_t n_classes) {
    size_t n_left = sum_counts(density->counters_left, n_classes);
    size_t n_right = sum_counts(density->counters_right, n_classes);
    size_t total = n_left + n_right;
    float left_rate = static_cast<float>(n_left) / static_cast<float>(total);
    float right_rate = static_cast<float>(n_right) / static_cast<float>(total);
    if (n_left == 0 || n_right == 0) {
        return COST_OF_EMPTINESS;
    }
    double left_cost = 0.0, right_cost = 0.0;
    size_t* counters_left = density->counters_left;
    size_t* counters_right = density->counters_right;
    if (n_left > 0) {
        size_t n_p;
        for (uint i = 0; i < n_classes; i++) {
            n_p = counters_left[i];
            if (n_p > 0) {
                left_cost += ShannonEntropy(static_cast<float>(n_p) / static_cast<float>(n_left));
            }
        }
        left_cost *= left_rate;
    }
    if (n_right > 0) {
        size_t n_n;
        for (uint i = 0; i < n_classes; i++) {
            n_n = counters_right[i];
            if (n_n > 0) {
                right_cost += ShannonEntropy(static_cast<float>(n_n) / static_cast<float>(n_right));
            }
        }
        right_cost *= right_rate;
    }
    return left_cost + right_cost;
}

void initRoot(Node* root, target_t* const targets, size_t n_instances, size_t n_classes) {
    memset(root->counters, 0x00, n_classes * sizeof(size_t));
    for (uint i = 0; i < n_instances; i++) {
        root->counters[targets[i]]++;
    }
}

Tree* ID3(data_t* const data, target_t* const targets, size_t n_instances,
                 size_t n_features, TreeConfig* config) {
    Node* current_node = newNode(config->n_classes);
    current_node->id = 0;
    current_node->n_instances = n_instances;
    current_node->score = 0.0;
    initRoot(current_node, targets, n_instances, config->n_classes);
    Node* child_node;
    Tree* tree = static_cast<Tree*>(malloc(sizeof(Tree)));
    tree->root = current_node;
    tree->config = config;
    tree->n_nodes = 1;
    tree->n_classes = config->n_classes;
    tree->n_features = n_features;
    bool still_going = 1;
    size_t* belongs_to = static_cast<size_t*>(calloc(n_instances, sizeof(size_t)));
    size_t** split_sides = static_cast<size_t**>(malloc(2 * sizeof(size_t*)));
    Splitter<target_t> splitter = {
        config->task,
        current_node,
        n_instances,
        nullptr,
        config->n_classes,
        belongs_to,
        static_cast<size_t>(NO_FEATURE),
        n_features,
        targets,
        config->nan_value
    };
    Density* densities = computeDensities(data, n_instances, n_features,
        config->n_classes, config->nan_value);
    Density* next_density;
    size_t best_feature = 0;
    std::queue<Node*> queue;
    queue.push(current_node);
    while ((tree->n_nodes < config->max_nodes) && !queue.empty() && still_going) {
        current_node = queue.front(); queue.pop();
        double e_cost = INFINITY;
        double lowest_e_cost = INFINITY;
        splitter.node = current_node;
        for (uint f = 0; f < n_features; f++) {
            splitter.feature_id = f;
            e_cost = evaluateByThreshold(&splitter, &densities[f], data, config->partitioning);
            if (e_cost < lowest_e_cost) {
                lowest_e_cost = e_cost;
                best_feature = f;
            }
        }
        next_density = &densities[best_feature];
        if ((best_feature != static_cast<size_t>(current_node->feature_id))
            || (next_density->split_value != current_node->split_value)) { // TO REMOVE ?
            next_density = &densities[best_feature];
            size_t split_totals[2] = {
                sum_counts(next_density->counters_left, config->n_classes),
                sum_counts(next_density->counters_right, config->n_classes)
            };
            if (split_totals[0] && split_totals[1]) {
                Node* new_children = static_cast<Node*>(malloc(2 * sizeof(Node)));
                data_t split_value = next_density->split_value;
                current_node->score = lowest_e_cost;
                current_node->feature_id = static_cast<int>(best_feature);
                current_node->split_value = split_value;
                current_node->left_child = &new_children[0];
                current_node->right_child = &new_children[1];

                split_sides[0] = next_density->counters_left;
                split_sides[1] = next_density->counters_right;
                for (uint i = 0; i < 2; i++) {
                    for (uint j = 0; j < n_instances; j++) {
                        bool is_on_the_left = (data[j * n_features + best_feature] < split_value) ? 1 : 0;
                        if (belongs_to[j] == static_cast<size_t>(current_node->id)) {
                            if (is_on_the_left * (1 - i) + (1 - is_on_the_left) * i) {
                                belongs_to[j] = tree->n_nodes;
                            }
                        }
                    }
                    child_node = &new_children[i];
                    child_node->id = static_cast<int>(tree->n_nodes);
                    child_node->split_value = split_value;
                    child_node->n_instances = split_totals[i];
                    child_node->score = COST_OF_EMPTINESS;
                    child_node->feature_id = static_cast<int>(best_feature);
                    child_node->left_child = nullptr;
                    child_node->right_child = nullptr;
                    child_node->counters = static_cast<size_t*>(malloc(config->n_classes * sizeof(size_t)));
                    memcpy(child_node->counters, split_sides[i], config->n_classes * sizeof(size_t));
                    if (lowest_e_cost > config->min_threshold) {
                        queue.push(child_node);
                    }
                    ++tree->n_nodes;
                }
            }
        }
    }
    free(belongs_to);
    free(split_sides);
    return tree;
}

float* classify(data_t* const data, size_t n_instances, size_t n_features,
                Tree* const tree, TreeConfig* config) {
    assert(config->task == gbdf_task::CLASSIFICATION_TASK);
    Node *current_node;
    size_t feature;
    size_t n_classes = config->n_classes;
    float* predictions = static_cast<float*>(malloc(n_instances * n_classes * sizeof(float)));
    for (uint k = 0; k < n_instances; k++) {
        bool improving = true;
        current_node = tree->root;
        while (improving) {
            feature = current_node->feature_id;
            if (current_node->left_child != NULL) {
                if (data[k * n_features + feature] >= current_node->split_value) {
                    current_node = current_node->right_child;
                }
                else {
                    current_node = current_node->left_child;
                }
            }
            else {
                improving = false;
            }
        }
        size_t node_instances = current_node->n_instances;
        for (uint c = 0; c < n_classes; c++) {
            predictions[k * n_classes + c] = static_cast<float>(current_node->counters[c]) / static_cast<float>(node_instances);
        }
    }
    return predictions;
}

data_t* regress(data_t* const data, size_t n_instances, size_t n_features,
                Tree* const tree, TreeConfig* config) {
    assert(config->task == gbdf_task::REGRESSION_TASK);
    Node *current_node;
    size_t feature;
    data_t* predictions = static_cast<data_t*>(malloc(n_instances * sizeof(data_t)));

    for (uint k = 0; k < n_instances; k++) {
        bool improving = true;
        current_node = tree->root;
        while (improving) {
            feature = current_node->feature_id;
            if (current_node->left_child != NULL) {
                if (data[k * n_features + feature] >= current_node->split_value) {
                    current_node = current_node->right_child;
                }
                else {
                    current_node = current_node->left_child;
                }
            }
            else {
                improving = false;
            }
        }
        // predictions[k] = stuff... -> TODO
        // TODO : define a new type of struct
    }
    return predictions;
}
