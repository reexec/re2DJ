#ifndef RE2DJ_TESTS_UNIT_TEST_SUPPORT_H_
#define RE2DJ_TESTS_UNIT_TEST_SUPPORT_H_

#include <cstdio>
#include <string>

// A deliberately small harness. Adding a test framework is a license and
// build-time decision that belongs in its own design note rather than in this
// scaffolding.
namespace re2dj::test
{

struct Context
{
    int checks = 0;
    int failures = 0;

    void Check(bool condition, const char* expression, const char* file, int line)
    {
        ++checks;
        if (condition)
        {
            return;
        }
        ++failures;
        std::fprintf(stderr, "%s:%d: FAILED %s\n", file, line, expression);
    }

    void Fail(const std::string& message, const char* file, int line)
    {
        ++checks;
        ++failures;
        std::fprintf(stderr, "%s:%d: FAILED %s\n", file, line, message.c_str());
    }
};

}  // namespace re2dj::test

#define RE2DJ_CHECK(context, expression) \
    (context).Check((expression), #expression, __FILE__, __LINE__)

#define RE2DJ_CHECK_EQ(context, actual, expected)                                \
    do                                                                           \
    {                                                                            \
        const auto& re2dj_actual = (actual);                                     \
        const auto& re2dj_expected = (expected);                                 \
        if (!(re2dj_actual == re2dj_expected))                                   \
        {                                                                        \
            (context).Fail(std::string(#actual) + " != " + #expected,            \
                           __FILE__,                                             \
                           __LINE__);                                            \
        }                                                                        \
        else                                                                     \
        {                                                                        \
            (context).Check(true, #actual, __FILE__, __LINE__);                  \
        }                                                                        \
    } while (false)

void RunCodeRegionScoreTests(re2dj::test::Context& context);
void RunCodeScanTests(re2dj::test::Context& context);
void RunPortHelperScanTests(re2dj::test::Context& context);
void RunImmediateScanTests(re2dj::test::Context& context);
void RunGuestPathTests(re2dj::test::Context& context);
void RunAddressSpaceTests(re2dj::test::Context& context);
void RunExecutionBackendTests(re2dj::test::Context& context);
void RunImportDispatcherTests(re2dj::test::Context& context);
void RunGuestModuleRegistryTests(re2dj::test::Context& context);
void RunGuestPeFacadeTests(re2dj::test::Context& context);
void RunKernel32ModuleTests(re2dj::test::Context& context);
void RunUser32ModuleTests(re2dj::test::Context& context);
void RunVersionTests(re2dj::test::Context& context);
void RunLauncherTests(re2dj::test::Context& context);
void RunLauncherUpdateTests(re2dj::test::Context& context);
void RunDdrawModuleTests(re2dj::test::Context& context);
void RunDsoundModuleTests(re2dj::test::Context& context);
void RunDinputModuleTests(re2dj::test::Context& context);
void RunWinmmModuleTests(re2dj::test::Context& context);
void RunGdiRasterTests(re2dj::test::Context& context);
void RunDirectXDisplayTests(re2dj::test::Context& context);
void RunDirectXSurfaceTests(re2dj::test::Context& context);
void RunDirectXDeviceTests(re2dj::test::Context& context);
void RunDirectXInputTests(re2dj::test::Context& context);
void RunGuestProcessTests(re2dj::test::Context& context);
void RunKernel32CrtTests(re2dj::test::Context& context);
void RunApiCallRecordTests(re2dj::test::Context& context);
void RunGuestFilesTests(re2dj::test::Context& context);
void RunGuestFilePrefetcherTests(re2dj::test::Context& context);
void RunGuestFileReadJournalTests(re2dj::test::Context& context);
void RunLoggingTests(re2dj::test::Context& context);
void RunImportEventLoopTests(re2dj::test::Context& context);
void RunPeImageTests(re2dj::test::Context& context);
void RunPeLoaderTests(re2dj::test::Context& context);
void RunHddRootTests(re2dj::test::Context& context);
void RunTargetProfileTests(re2dj::test::Context& context);
void RunVfsFileTableTests(re2dj::test::Context& context);
void RunLptdiChallengeResponseTests(re2dj::test::Context& context);
void RunBitmapFileTests(re2dj::test::Context& context);
void RunLptdiResponseProfileTests(re2dj::test::Context& context);
void RunHardlockHandshakeResponseTests(re2dj::test::Context& context);
void RunHardlockApiDescriptorTests(re2dj::test::Context& context);
void RunHardlockProtocolTests(re2dj::test::Context& context);
void RunHardlockDeviceTests(re2dj::test::Context& context);
void RunHardlockPayloadResponsesTests(re2dj::test::Context& context);
void RunHardlockTransformResponsesTests(re2dj::test::Context& context);
void RunHardlockEngineTests(re2dj::test::Context& context);
void RunHardlockMaterialConfigTests(re2dj::test::Context& context);
void RunHardlockDeviceMaterialTests(re2dj::test::Context& context);
void RunLegacyIoPortBusTests(re2dj::test::Context& context);
void RunLegacyIoTrapTests(re2dj::test::Context& context);
void RunEz2DjIoBoardTests(re2dj::test::Context& context);
void RunEz2DancerIoBoardTests(re2dj::test::Context& context);
void RunLegacyDrawCommandTests(re2dj::test::Context& context);
void RunPresentationFilterTests(re2dj::test::Context& context);
void RunPresentIntervalHistogramTests(re2dj::test::Context& context);
void RunPostShaderTests(re2dj::test::Context& context);
void RunGlRendererIdentityTests(re2dj::test::Context& context);
void RunPresentSyncTests(re2dj::test::Context& context);
void RunWindowPolicyTests(re2dj::test::Context& context);
void RunEz2DjKeyboardMapTests(re2dj::test::Context& context);
void RunGamepadBindingsTests(re2dj::test::Context& context);
void RunGuestFindTests(re2dj::test::Context& context);
void RunLegacyTextureTests(re2dj::test::Context& context);
void RunTrueColorTests(re2dj::test::Context& context);
void RunLegacyTransformTests(re2dj::test::Context& context);
void RunLegacyVertexBufferTests(re2dj::test::Context& context);
void RunLegacyAudioBufferTests(re2dj::test::Context& context);
void RunDirectSoundBufferPolicyTests(re2dj::test::Context& context);
void RunDirectSoundDeviceTests(re2dj::test::Context& context);
void RunMameChdTests(re2dj::test::Context& context);
void RunFat32ChdTests(re2dj::test::Context& context);
void RunChdHunkCacheTests(re2dj::test::Context& context);
void RunFat32DirectoryNameTests(re2dj::test::Context& context);
void RunChdExtractNameTests(re2dj::test::Context& context);

#endif  // RE2DJ_TESTS_UNIT_TEST_SUPPORT_H_
