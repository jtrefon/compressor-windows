#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::CompressorWindows::implementation {
App::App() { InitializeComponent(); }

void App::OnLaunched(LaunchActivatedEventArgs const &) {
  window = make<MainWindow>();
  window.Activate();
}
} // namespace winrt::CompressorWindows::implementation