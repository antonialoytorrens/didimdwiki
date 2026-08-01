document.addEventListener("DOMContentLoaded", function () {
	var ta = document.querySelector("textarea[name=body]");

	if (ta) {
		ta.addEventListener("keydown", function (e) {
			if (e.key !== "Tab" || e.shiftKey)
				return;
			e.preventDefault();
			var s = ta.selectionStart, t = ta.selectionEnd;
			ta.value = ta.value.slice(0, s) + "\t" + ta.value.slice(t);
			ta.selectionStart = ta.selectionEnd = s + 1;
		});
	}

	document.querySelectorAll("button.danger").forEach(function (b) {
		b.addEventListener("click", function (e) {
			if (!window.confirm("Are you sure? This cannot be undone."))
				e.preventDefault();
		});
	});
});
