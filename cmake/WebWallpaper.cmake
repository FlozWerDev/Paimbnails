if (WIN32)
    CPMAddPackage(
        NAME WebView2
        VERSION 1.0.4314.35
        URL https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.4314.35/microsoft.web.webview2.1.0.4314.35.nupkg
        URL_HASH SHA256=90778DEB1B77E36292828B4B5833C9255C27533F40E0DDD55891AE718944EDC4
        DOWNLOAD_ONLY YES
    )
    target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${WebView2_SOURCE_DIR}/build/native/include)
    target_link_libraries(${PROJECT_NAME} ${WebView2_SOURCE_DIR}/build/native/x64/WebView2LoaderStatic.lib version.lib)
endif()
