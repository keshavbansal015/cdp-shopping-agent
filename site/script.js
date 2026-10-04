"use strict";

var MAX_QTY = 3;
var RAND_SEED = 42;
var PRODUCTS = [
    { id: "blue-mug", name: "Blue Mug", price: 12, description: "A sturdy ceramic mug that holds 350 ml." },
    { id: "red-hat", name: "Red Hat", price: 20, description: "A soft cotton cap in bright red." },
    { id: "green-lamp", name: "Green Lamp", price: 35, description: "A compact desk lamp with a warm light." },
    { id: "yellow-notebook", name: "Yellow Notebook", price: 6, description: "A 120-page notebook with lined paper." }
];

function findProduct(id) {
    return PRODUCTS.filter(p => p.id === id)[0];
}

function readGoal() {
    // example: index.html?item=blue-mug&qty=2&seed=42&popup_p=0.15&delay_p=0.10
    var params = new URLSearchParams(location.search);
    var item = params.get("item", "");
    var qty = parseInt(params.get("qty"), 10);
    var seed = parseInt(params.get("seed"), 10);
    var popup_p = parseFloat(params.get("popup_p"));
    var delay_p = parseFloat(params.get("delay_p"));
    return {
        item: item,
        qty: isNaN(qty) ? 1 : Math.min(MAX_QTY, Math.max(1, qty)),
        seed: isNaN(seed) ? RAND_SEED : seed,
        popup_p: isNaN(popup_p) ? 0.1 : Math.max(0, Math.min(1, popup_p)),
        delay_p: isNaN(delay_p) ? 0.05 : Math.max(0, Math.min(1, delay_p))
    };
}
var goal = readGoal();
var app = document.getElementById("app");
var overlay = null;
var delayTimer = null;

// STATE OF THE PAGE
var state = {
    screen: "catalog",
    product: null,
    qty: 0,
    cart: [],
    order: null,
    message: ""
}

function money(n) {
    // this function is used to format the number as currency
    return "$" + n.toFixed(2) + " ";
}

function cartCount() {
    // calculate the number of items in the cart, it can be zero
    return state.cart.reduce((sum, line) => sum + line.qty, 0);
}

function cartTotal(lines) {
    return lines.reduce((sum, line) => sum + findProduct(line.id).price * line.qty, 0);
}

function cartButton() {
    return "<button data-act='cart'>Cart (" + cartCount() + ")</button>";
}

function renderProduct() {
    var product = state.product;
    var res = "<h2>" + product.name + "</h2>";
    res += "<p>" + product.description + "</p>";
    res += "<p class='muted'>" + money(product.price) + " each</p>";
    res += "<div class='qty'>";
    res += "<button data-act='dec'>-</button>";
    res += "<output id='qty'>" + state.qty + "</output>";
    res += "<button data-act='inc'>+</button></div>";
    res += "<button class='primary' data-act='add'>Add to cart</button>";
    res += cartButton();
    res += "<button data-act='back'>Back</button>";
    res += "<div class='note' id='note'>" + state.message + "</div>";
    return res;
}

function viewProduct(btn) {
    state.product = findProduct(btn.getAttribute("data-id"));
    state.qty = 1;
    goTo("product");
}

function incCount() {
    if (state.qty < MAX_QTY) { state.qty++; document.getElementById("qty").textContent = state.qty; }
}

function decCount() {
    if (state.qty > 1) { state.qty--; document.getElementById("qty").textContent = state.qty; }
}

function renderCatalog() {
    return "<h2>Catalog</h2><ul class='ul.products'>" + PRODUCTS.map(product => {
        var res = "<li><span>" + product.name + "<span class='.price'> ";
        res += money(product.price) + "</span></span>";
        res += "<button data-act='view' data-id='" + product.id + "'>" + product.id + "</button></li>";
        return res;
    }).join("") + "</ul>" + cartButton() + "<button data-act='newsletter'>Newsletter</button>";
}

function renderNewsletter() {
    return "<h2>Newsletter</h2>" +
        "<p>Nothing to see here.</p>" +
        "<button data-act='back'>Back</button>";
}

function linesHtml(lines) {
    // take an array of line objects and return an HTML string, for screen: cart and done
    return "<ul class='list'>" + lines.map(line => {
        var product = findProduct(line.id);
        var res = "<li>" + product.name + " \u00d7 ";
        res += line.qty + " @ " + money(product.price);
        res += " = " + money(product.price * line.qty) + "</li>";
        return res;
    }).join("") + "</ul><div class='total'>Total: " + money(cartTotal(lines)) + "</div>";
}

function renderCart() {
    var empty = state.cart.length === 0;
    return "<h2>Cart</h2>" +
        (empty ? "<p>Your cart is empty.</p>" : linesHtml(state.cart)) +
        "<button class='primary' data-act='checkout'" + (empty ? " disabled" : "") + ">Checkout</button>" +
        "<button data-act='clear'" + (empty ? " disabled" : "") + ">Clear cart</button>" +
        "<button data-act='back'>Back</button>";
}

function renderDone() {
    return "<h2>Order placed</h2>" + linesHtml(state.order);
}

var RENDERERS = {
    catalog: renderCatalog,
    product: renderProduct,
    cart: renderCart,
    done: renderDone,
    newsletter: renderNewsletter
};

function addToCart(id, qty) {
    // adds a line or updates line
    var line = state.cart.filter(line => line.id === id)[0];
    if (line) {
        line.qty += qty;
    } else {
        state.cart.push({ id, qty });
    }
}

function clearCart() { state.cart = []; }

function updateCart() {
    addToCart(state.product.id, state.qty);
    state.message = "Added " + state.qty + " \u00d7 " + state.product.name + " to cart.";
    render();   // stay on the product screen; refreshes the cart count
}

function checkout() {
    if (state.cart.length === 0) return false;
    state.order = state.cart.map(line => ({ id: line.id, qty: line.qty }));
    clearCart();
    return true;
}

function render() {
    app.innerHTML = RENDERERS[state.screen]();
    document.body.setAttribute("data-screen", state.screen);
}

app.addEventListener("click", (e) => {
    var btn = e.target.closest("button");
    if (!btn || btn.disabled) return;
    var action = ACTIONS[btn.getAttribute("data-act")];
    if (action) action(btn);
});

function goTo(screen) {
    state.screen = screen;
    state.message = "";
    render();
    afterScreenChange(screen);
}

function ensureOverlay() {
    if (overlay) return overlay;
    overlay = document.createElement("div");
    overlay.id = "overlay";
    overlay.hidden = true;
    overlay.setAttribute("role", "dialog");
    overlay.setAttribute("aria-modal", "true");
    overlay.innerHTML =
        "<div class='box'>" +
        "<h3>This is a popup!</h3>" +
        "<p>Do you like popups?</p>" +
        "<button id='dismiss'>Dismiss</button>" +
        "</div>";
    overlay.querySelector("#dismiss").addEventListener("click", hidePopup);
    document.body.appendChild(overlay);
    return overlay;
}

function hidePopup() {
    if (overlay) overlay.hidden = true;
    app.inert = false;
    var firstButton = app.querySelector("button");
    if (firstButton) firstButton.focus();
}

function showPopup() {
    ensureOverlay().hidden = false;
    app.inert = true;   // nothing underneath can be clicked or focused
    overlay.querySelector("#dismiss").focus();
}


function delayButtons(ms) {
    app.classList.add("late");   // hides all buttons inside #app
    delayTimer = setTimeout(() => {
        app.classList.remove("late");
        delayTimer = null;
    }, ms);

}

function makeRng(seed) {
    return function () {
        let t = seed += 0x6D2B79F5;
        t = Math.imul(t ^ (t >>> 15), t | 1);
        t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
        return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    }
}
var rand = makeRng(goal.seed);

function afterScreenChange(screen) {
    // reset leftovers from the previous screen
    clearTimeout(delayTimer);
    delayTimer = null;
    app.classList.remove("late");

    if (screen !== "done") {
        var rPopup = rand();
        var rDelay = rand();
        var rMs = rand();

        if (rDelay < goal.delay_p) {
            delayButtons(50 + Math.floor(rMs * 251));   // 50..300 ms
        }
        if (rPopup < goal.popup_p) {
            showPopup();
        }
    }
}

var ACTIONS = {
    view: viewProduct,
    cart: () => { goTo("cart"); },
    newsletter: () => { goTo("newsletter"); },
    back: () => { goTo("catalog"); },
    dec: decCount,
    inc: incCount,
    add: updateCart,
    clear: () => { clearCart(); render(); },
    checkout: () => { if (checkout()) goTo("done"); }
};


window.miniShop = {
    goal: goal,
    get order() { return state.order ? state.order.slice() : null; },
    get success() {
        var o = state.order;
        return !!o && o.length === 1 && o[0].id === goal.item && o[0].qty === goal.qty;
    }
};

function init() {
    goTo("catalog");
}
init();