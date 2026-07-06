#include "logging.hpp"

#include <fstream>

std::mutex logging_mutex;

uint plot_counter = 0;

void log_data(std::vector<double> &data, int pid, std::string custom_flag, std::string title, std::string data_label) {
    CHECK(custom_flag != "" && custom_flag.find('#') == std::string::npos && custom_flag.find(' ') == std::string::npos &&
              custom_flag.find('_') == std::string::npos,
          "Custom flag cannot contain these characters:[' ', '#', '_']")
    CHECK(title.find('#') == std::string::npos, "Plot labels cannot contain the # character")

    std::string filename = "plotdata_" + std::to_string(pid) + "_" + std::to_string(plot_counter) + "_" + custom_flag;
    plot_counter++;

    std::ofstream stream(filename);
    CHECK(stream.is_open(), "Could not open output file " + filename)

    stream << "#" << title;
    stream << "#" << (data_label == "" ? "-" : data_label);
    stream << std::endl;

    for (size_t row = 0; row < data.size(); row++)
        stream << data.at(row) << std::endl;
    stream.close();
}

void log_data(std::vector<std::vector<double>> &data, int pid, std::string custom_flag, std::string title, std::vector<std::string> data_labels) {
    CHECK(data.size() > 0, "At least one dataset must be specified")
    CHECK(custom_flag != "" && custom_flag.find('#') == std::string::npos && custom_flag.find(' ') == std::string::npos &&
              custom_flag.find('_') == std::string::npos,
          "Custom flag cannot contain these characters:[' ', '#', '_']")
    CHECK(title.find('#') == std::string::npos, "Plot labels cannot contain the # character")

    std::string filename = "plotdata_" + std::to_string(pid) + "_" + std::to_string(plot_counter) + "_" + custom_flag;
    plot_counter++;

    std::ofstream stream(filename);
    CHECK(stream.is_open(), "Could not open output file " + filename)

    stream << "#" << title;
    for (size_t column = 0; column < data.size(); column++) {
        if (column < data_labels.size())
            stream << "#" << data_labels.at(column);
        else
            stream << "#-";
    }
    stream << std::endl;

    size_t max_data_size = 0;
    for (std::vector<double> &dataset : data)
        max_data_size = std::max(max_data_size, dataset.size());

    for (size_t row = 0; row < max_data_size; row++) {
        for (size_t col = 0; col < data.size(); col++) {
            if (row < data.at(col).size())
                stream << data.at(col).at(row);
            else
                stream << "NaN";

            if (col < data.size() - 1)
                stream << " ";
        }
        stream << std::endl;
    }
    stream.close();
}

std::string payload_str(size_t size) {
    const size_t KB = 1024;
    const size_t MB = 1024 * 1024;
    const size_t GB = 1024 * 1024 * 1024;

    if (size % GB == 0)
        return std::to_string(size / GB) + "GB";
    else if (size % MB == 0)
        return std::to_string(size / MB) + "MB";
    else if (size % KB == 0)
        return std::to_string(size / KB) + "KB";
    else
        return std::to_string(size) + "B";
}
