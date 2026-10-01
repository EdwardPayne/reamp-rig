#include "App/CommandLine.h"

namespace rf::test
{
    using rf::app::LaunchOptions;

    /*  Command-line parsing of the development checks (review 2026-10-01, A7): a batch or sync
        check runs on the virtual loopback unless its *own* hardware flag is given, so one
        check's hardware flag never lets the other onto the real interface.
    */
    class CommandLineTests final : public juce::UnitTest
    {
    public:
        CommandLineTests() : juce::UnitTest ("CommandLine", "CommandLine") {}

        void runTest() override
        {
            const auto cwd = juce::File::getSpecialLocation (juce::File::tempDirectory);
            const auto parse = [&cwd] (const juce::StringArray& args) { return LaunchOptions::parse (args, cwd); };

            beginTest ("checks run on the virtual loopback by default");
            {
                const auto batch = parse ({ "--batch-check=out" });
                expect (batch.virtualDevice);
                expect (batch.virtualLoopbackDelay == std::optional<int> (300));

                const auto sync = parse ({ "--sync-check" });
                expect (sync.virtualDevice);
                expect (sync.virtualLoopbackDelay == std::optional<int> (300));
            }

            beginTest ("each check's own hardware flag allows the real device");
            {
                const auto batch = parse ({ "--batch-check=out", "--batch-check-hardware" });
                expect (! batch.virtualDevice);
                expect (! batch.virtualLoopbackDelay.has_value());

                const auto sync = parse ({ "--sync-check", "--sync-check-hardware" });
                expect (! sync.virtualDevice);

                const auto both = parse ({ "--sync-check", "--sync-check-hardware", "--batch-check=out", "--batch-check-hardware" });
                expect (! both.virtualDevice);
            }

            beginTest ("the other check's hardware flag does not");
            {
                const auto batch = parse ({ "--batch-check=out", "--sync-check-hardware" });
                expect (batch.virtualDevice, "--batch-check stays on the virtual loopback");

                const auto sync = parse ({ "--sync-check", "--batch-check-hardware" });
                expect (sync.virtualDevice, "--sync-check stays on the virtual loopback");

                const auto mixed = parse ({ "--sync-check", "--sync-check-hardware", "--batch-check=out" });
                expect (mixed.virtualDevice, "one check needs the loopback, so both run on it");
            }

            beginTest ("an explicit loopback delay is kept; --virtual-reject-rate lists rates");
            {
                const auto o = parse ({ "--batch-check=out", "--virtual-loopback=37", "--virtual-reject-rate=44100,96000" });
                expect (o.virtualLoopbackDelay == std::optional<int> (37));
                expectEquals (o.virtualRejectRates.size(), 2);
                expect (o.virtualRejectRates.contains (44100.0) && o.virtualRejectRates.contains (96000.0));
            }
        }
    };

    static CommandLineTests commandLineTests;
}
