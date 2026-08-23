(() => {
  const progress = document.querySelector(".reading-progress");
  const updateProgress = () => {
    if (!progress) return;
    const root = document.documentElement;
    const distance = root.scrollHeight - root.clientHeight;
    const ratio = distance > 0 ? root.scrollTop / distance : 0;
    progress.style.transform = `scaleX(${Math.min(1, Math.max(0, ratio))})`;
  };

  updateProgress();
  document.addEventListener("scroll", updateProgress, { passive: true });

  const currentLesson = document.body.dataset.lesson;
  if (currentLesson) {
    localStorage.setItem("gateway-course-last-lesson", currentLesson);
  }
})();
