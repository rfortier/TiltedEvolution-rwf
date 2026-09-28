#pragma once
#include "UIInterop.h"

namespace UI {
	class OStimMenu : public RE::IMenu {
	
	public:
		OStimMenu(std::string menuName);
		static void Register(std::string menuName, RE::UI::Create_t* createFn);
        inline bool isOpen() { return _isOpen; }
		void Show();
		void Hide();
		void Update() {
			while (!_taskQueue.empty()) {
				auto& task = _taskQueue.front();
				task();
				_taskQueue.pop();
			}
		};
		virtual void AdvanceMovie(float a_interval, std::uint32_t a_currentTime) override;
		virtual void Handle(UI::Controls control)=0;
		virtual void PostRegister();

		void OverrideFunction(RE::GFxValue& movieClip, RE::GFxFunctionHandler* fn, const char* name) {
			RE::GFxValue dst;
			_view->CreateFunction(&dst, fn);
			movieClip.SetMember(name, dst);
		}

	public:
		std::string menuName;

	private:
		class Logger : public RE::GFxLog {
		public:
			void LogMessageVarg(LogMessageType, const char* a_fmt, std::va_list a_argList) override {
				std::string fmt(a_fmt ? a_fmt : "");
				while (!fmt.empty() && fmt.back() == '\n') {
					fmt.pop_back();
				}

				std::va_list args;
				va_copy(args, a_argList);
				std::vector<char> buf(static_cast<std::size_t>(std::vsnprintf(0, 0, fmt.c_str(), a_argList) + 1));
				std::vsnprintf(buf.data(), buf.size(), fmt.c_str(), args);
				va_end(args);

				logger::info("{}"sv, buf.data());
			}
		};
	protected:
		using UITask = std::function<void()>;

		bool _isOpen = false;
		RE::GPtr<RE::GFxMovieView> _view;

		using Lock = std::recursive_mutex;
		using Locker = std::lock_guard<Lock>;
		mutable Lock _lock;		

		std::queue<UITask> _taskQueue;
	protected:
		void QueueUITask(std::function<void()> fn) { 
			//Show();
			Locker locker(_lock);
			_taskQueue.push(std::move(fn));
		}
		void GetRoot(RE::GFxValue& root);
	};



}
