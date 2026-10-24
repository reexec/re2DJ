#include <cstdio>

#include "test_support.h"

int main()
{
    re2dj::test::Context context;

    RunAddressSpaceTests(context);
    RunCodeRegionScoreTests(context);
    RunCodeScanTests(context);
    RunPortHelperScanTests(context);
    RunImmediateScanTests(context);
    RunExecutionBackendTests(context);
    RunLoggingTests(context);
    RunVersionTests(context);
    RunLauncherTests(context);
    RunLauncherUpdateTests(context);
    RunImportDispatcherTests(context);
    RunGuestModuleRegistryTests(context);
    RunGuestPeFacadeTests(context);
    RunKernel32ModuleTests(context);
    RunUser32ModuleTests(context);
    RunDdrawModuleTests(context);
    RunDsoundModuleTests(context);
    RunDinputModuleTests(context);
    RunWinmmModuleTests(context);
    RunGdiRasterTests(context);
    RunDirectXDisplayTests(context);
    RunDirectXSurfaceTests(context);
    RunDirectXDeviceTests(context);
    RunDirectXInputTests(context);
    RunGuestProcessTests(context);
    RunKernel32CrtTests(context);
    RunApiCallRecordTests(context);
    RunGuestFilesTests(context);
    RunGuestFilePrefetcherTests(context);
    RunGuestFileReadJournalTests(context);
    RunImportEventLoopTests(context);
    RunGuestPathTests(context);
    RunPeImageTests(context);
    RunPeLoaderTests(context);
    RunHddRootTests(context);
    RunTargetProfileTests(context);
    RunVfsFileTableTests(context);
    RunLptdiChallengeResponseTests(context);
    RunBitmapFileTests(context);
    RunLptdiResponseProfileTests(context);
    RunHardlockHandshakeResponseTests(context);
    RunHardlockApiDescriptorTests(context);
    RunHardlockProtocolTests(context);
    RunHardlockDeviceTests(context);
    RunHardlockPayloadResponsesTests(context);
    RunHardlockTransformResponsesTests(context);
    RunHardlockEngineTests(context);
    RunHardlockMaterialConfigTests(context);
    RunHardlockDeviceMaterialTests(context);
    RunEz2DjIoBoardTests(context);
    RunEz2DancerIoBoardTests(context);
    RunLegacyIoPortBusTests(context);
    RunLegacyIoTrapTests(context);
    RunLegacyDrawCommandTests(context);
    RunPresentationFilterTests(context);
    RunPresentIntervalHistogramTests(context);
    RunPostShaderTests(context);
    RunPostShaderPresetTests(context);
    RunGlRendererIdentityTests(context);
    RunPresentSyncTests(context);
    RunWindowPolicyTests(context);
    RunEz2DjKeyboardMapTests(context);
    RunGamepadBindingsTests(context);
    RunGuestFindTests(context);
    RunLegacyTextureTests(context);
    RunTrueColorTests(context);
    RunLegacyTransformTests(context);
    RunLegacyVertexBufferTests(context);
    RunLegacyAudioBufferTests(context);
    RunDirectSoundBufferPolicyTests(context);
    RunDirectSoundDeviceTests(context);
    RunMameChdTests(context);
    RunFat32ChdTests(context);
    RunChdHunkCacheTests(context);
    RunFat32DirectoryNameTests(context);
    RunChdExtractNameTests(context);

    std::printf("checks: %d, failures: %d\n", context.checks, context.failures);
    return context.failures == 0 ? 0 : 1;
}
