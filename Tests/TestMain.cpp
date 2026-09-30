#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

/*  Test runner for all JUCE UnitTest cases in this executable.

    Usage: ReampForgeTests [--category=<name>]
    Exit code is the number of failed test cases (0 = all passed), which is what ctest uses.
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
