#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

/*  Test runner for all JUCE UnitTest cases in this executable.

    Usage: ReampRigTests [--category=<name>]
    Exit code 0 when every check passed, 1 when any check failed or no test ran (what ctest
    uses). A category is the second argument of each juce::UnitTest's constructor; CMake
    registers one ctest entry per Tests/<Category>Tests.cpp.
*/
int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit; // message manager for code that posts to it

    juce::ArgumentList args (argc, argv);
    const auto category = args.getValueForOption ("--category");

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);

    if (category.isNotEmpty())
        runner.runTestsInCategory (category);
    else
        runner.runAllTests();

    int failures = 0, passes = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        const auto* r = runner.getResult (i);
        failures += r->failures;
        passes += r->passes;
    }

    std::printf ("\n%d test cases, %d checks passed, %d failed\n", runner.getNumResults(), passes, failures);

    if (runner.getNumResults() == 0)
    {
        std::printf ("No tests found%s\n", category.isNotEmpty() ? (" in category " + category).toRawUTF8() : "");
        return 1;
    }

    return failures > 0 ? 1 : 0;
}
