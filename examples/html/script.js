window.addEventListener("contextmenu", (e) => {
    e.preventDefault();
});

document.body.addEventListener("contextmenu", (e) => {
    console.log("Own contextmenu");
});