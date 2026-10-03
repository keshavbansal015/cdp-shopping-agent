
"use strict";

var MAX_QTY = 10;
var PRODUCTS = [
    { id: "blue-mug", name: "Blue Mug", price: 12, description: "A sturdy ceramic mug that holds 350 ml." },
    { id: "red-hat", name: "Red Hat", price: 20, description: "A soft cotton cap in bright red." },
    { id: "green-lamp", name: "Green Lamp", price: 35, description: "A compact desk lamp with a warm light." },
    { id: "yellow-notebook", name: "Yellow Notebook", price: 6, description: "A 120-page notebook with lined paper." }
];

function readGoal() {
    // example: index.html?item=blue-mug&qty=2&seed=42&popup_p=0.15&delay_p=0.10
    var params = new URLSearchParams(location.search);
    var item = params.get("item", "");
    var qty = parseInt(params.get("qty"), 10);
    var seed = 42;
    var popup_p = parseFloat(params.get("popup_p"));
    var delay_p = parseFloat(params.get("delay_p"));
    return {
        item: item,
        qty: isNaN(qty) ? 1 : Math.min(MAX_QTY, Math.max(1, qty)),
        seed: seed,
        popup_p: isNaN(popup_p) ? 0.1 : Math.max(0, Math.min(1, popup_p)),
        delay_p: isNaN(delay_p) ? 0.05 : Math.max(0, Math.min(1, delay_p))
    };
}

function createTestGoal(params) {
    // if params not provided, create a random goal
    // params = { item, qty, seed, popup_p, delay_p }
    var item = !params.item ? PRODUCTS[Math.floor(Math.random() * PRODUCTS.length)].id : params.item;
    var qty = !params.qty ? Math.floor(Math.random() * MAX_QTY) + 1 : params.qty;
    var seed = !params.seed ? Math.floor(Math.random() * 100) : params.seed;
    var popup_p = !params.popup_p ? 0.1 : parseFloat(params.popup_p);
    var delay_p = !params.delay_p ? 0.05 : parseFloat(params.delay_p);
    var queryString = "?item=" + item + "&qty=" + qty + "&seed=" + seed + "&popup_p=" + popup_p + "&delay_p=" + delay_p;
    return queryString;
}

