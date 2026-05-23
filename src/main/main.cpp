#include "market_data/EventPipeline.hpp"
#include "market_data/MarketDataPrinter.hpp"
#include "order_book/BookDispatcher.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace cmf;

namespace
{

struct CliOptions
{
    std::string mode;
    std::filesystem::path file;
    std::filesystem::path folder;
    std::string merge = "both";
    std::size_t snapshotEvery = 100000;
};

void printUsage(std::ostream& output, const char* program)
{
    output << "Usage:\n"
           << "  " << program << " task1 --file <ndjson>\n"
           << "  " << program << " task1-hard --folder <dir> --merge flat|hierarchy|both\n"
           << "  " << program << " task2 --file <ndjson> --snapshot-every <N>\n"
           << "  " << program << " task2-hard --folder <dir> --merge flat|hierarchy|both --snapshot-every <N>\n";
}

std::string_view requireValue(int& index, int argc, const char* argv[], std::string_view option)
{
    if (index + 1 >= argc)
    {
        throw std::runtime_error(std::string{"missing value for "} + std::string{option});
    }
    ++index;
    return argv[index];
}

CliOptions parseArgs(int argc, const char* argv[])
{
    if (argc < 2)
    {
        throw std::runtime_error("missing mode");
    }

    CliOptions options;
    options.mode = argv[1];

    for (int i = 2; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg == "--file")
        {
            options.file = requireValue(i, argc, argv, arg);
        }
        else if (arg == "--folder")
        {
            options.folder = requireValue(i, argc, argv, arg);
        }
        else if (arg == "--merge")
        {
            options.merge = requireValue(i, argc, argv, arg);
            if (options.merge != "flat" && options.merge != "hierarchy" && options.merge != "both")
            {
                throw std::runtime_error("--merge must be flat, hierarchy, or both");
            }
        }
        else if (arg == "--snapshot-every")
        {
            options.snapshotEvery = static_cast<std::size_t>(std::stoull(std::string{requireValue(i, argc, argv, arg)}));
        }
        else
        {
            throw std::runtime_error("unknown argument: " + std::string{arg});
        }
    }

    return options;
}

std::vector<MergeStrategy> requestedStrategies(const std::string& merge)
{
    if (merge == "both")
    {
        return {MergeStrategy::Flat, MergeStrategy::Hierarchy};
    }
    return {parseMergeStrategy(merge)};
}

void printEvents(const char* title, const std::vector<MarketDataEvent>& events)
{
    std::cout << title << ":\n";
    for (const auto& event : events)
    {
        processMarketDataEvent(event, std::cout);
    }
}

void printStats(const char* name, const DispatchResult& result)
{
    std::cout << name << " total events: " << result.stats.totalEvents << '\n'
              << name << " first timestamp: " << result.firstTimestamp << '\n'
              << name << " last timestamp: " << result.lastTimestamp << '\n'
              << name << " seconds: " << result.stats.seconds << '\n'
              << name << " throughput: " << result.stats.throughput << " events/sec\n";
}

void runTask1(const CliOptions& options)
{
    if (options.file.empty())
    {
        throw std::runtime_error("task1 requires --file");
    }

    const DispatchResult result = processSingleFile(options.file, {});
    printEvents("First 10 events", result.firstEvents);
    printEvents("Last 10 events", result.lastEvents);
    printStats("Task1", result);
}

void runTask1Hard(const CliOptions& options)
{
    if (options.folder.empty())
    {
        throw std::runtime_error("task1-hard requires --folder");
    }

    const auto files = listMarketDataFiles(options.folder);
    std::cout << "Files: " << files.size() << '\n';

    for (const auto strategy : requestedStrategies(options.merge))
    {
        const DispatchResult result = processMergedFiles(files, strategy, {});
        const std::string name = std::string{"Task1-hard "} + mergeStrategyName(strategy);
        printEvents("First 10 events", result.firstEvents);
        printEvents("Last 10 events", result.lastEvents);
        printStats(name.c_str(), result);
    }
}

void runTask2(const CliOptions& options)
{
    if (options.file.empty())
    {
        throw std::runtime_error("task2 requires --file");
    }

    AsyncSnapshotPrinter snapshotPrinter{std::cout};
    BookDispatcher dispatcher{
        options.snapshotEvery,
        [&](std::string snapshot)
        {
            snapshotPrinter.submit(std::move(snapshot));
        }};

    const DispatchResult result = processSingleFile(
        options.file,
        [&](const MarketDataEvent& event)
        {
            dispatcher.process(event);
        });

    snapshotPrinter.close();
    printStats("Task2", result);
    dispatcher.printFinalBest(std::cout);
}

void runTask2Hard(const CliOptions& options)
{
    if (options.folder.empty())
    {
        throw std::runtime_error("task2-hard requires --folder");
    }

    const auto files = listMarketDataFiles(options.folder);
    std::cout << "Files: " << files.size() << '\n';

    for (const auto strategy : requestedStrategies(options.merge))
    {
        AsyncSnapshotPrinter snapshotPrinter{std::cout};
        BookDispatcher dispatcher{
            options.snapshotEvery,
            [&](std::string snapshot)
            {
                snapshotPrinter.submit(std::move(snapshot));
            }};

        const DispatchResult result = processMergedFiles(
            files,
            strategy,
            [&](const MarketDataEvent& event)
            {
                dispatcher.process(event);
            });

        snapshotPrinter.close();
        const std::string name = std::string{"Task2-hard "} + mergeStrategyName(strategy);
        printStats(name.c_str(), result);
        dispatcher.printFinalBest(std::cout);
    }
}

} // namespace

int main(int argc, const char* argv[])
{
    try
    {
        const CliOptions options = parseArgs(argc, argv);

        if (options.mode == "task1")
        {
            runTask1(options);
        }
        else if (options.mode == "task1-hard")
        {
            runTask1Hard(options);
        }
        else if (options.mode == "task2")
        {
            runTask2(options);
        }
        else if (options.mode == "task2-hard")
        {
            runTask2Hard(options);
        }
        else
        {
            printUsage(std::cerr, argv[0]);
            return 1;
        }
    }
    catch (std::exception& ex)
    {
        std::cerr << "Back-tester threw an exception: " << ex.what() << '\n';
        printUsage(std::cerr, argv[0]);
        return 1;
    }

    return 0;
}
