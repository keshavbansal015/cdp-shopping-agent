## Answering questions

- What is my state, actions, and reward?
    -  state: 
        ```
        |goal:<targetItem>x<targetQty>
        The goal we are trying to achieve.
        |scr:<screen>
        The current screen/view name from the observation (e.g., scr:catalog, scr:product, scr:cart).
        |pop:<0 or 1>
        A flag indicating whether a popup modal is currently active (1 for true, 0 for false).
        |pid:<productId>
        The ID of the product currently focused or viewed (if any).
        |pick:<qty>
        The currently selected quantity for the product on the product page.
        |cart:<cartSummary>
        A summary of all items currently inside the shopping cart (e.g., yellow-notebookx1,).
        |btns:<btnSummary>
        A bracketed summary text of all currently clickable buttons on the screen (e.g., [blue-mug][red-hat][Cart (0)]).
        ```
    - action: 
    ```
        "wait", and list of button indices (0, 1, 2, ...n)
        where n is the number of clickable buttons
    ```
    - reward:
    ```
        - If we reach the final state: 20
        - If we go over step limit: -10
        - Every step costs -0.5
        - If # of items in cart == target qty: +3
        - If # of items in cart < target qty: +1.5
        - If # of items in cart > target qty: -0.8

        * Idea was to encourage the model to pick up items with the desired quantity.
        
    ```
    - Episode End: 
    ```
        - If we reach the target state.
        - If we click on checkout.
        - If we go over step limit.
    ```

- If I had more time, I would like to re-write the code with more clarity, modularity. Right now the functions are not designed with a single responsibility in mind. 
- I would also like to implement a logging module to make it more modular rather than hardcoding it.
- A great addition could also be some unit tests and visualizations while training for the Q-table.

What I skipped:
- I skipped proper documentation for Part 4, and optional Part 5.
