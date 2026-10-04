# cdp-shopping-agent

## Setup
To run this project locally:
1. Download and install the required cpp and python libraries along with `chromium`
    - cpp=17+: lohmann/json, boost/asio, boost/beast
    - python=3.12+: pandas, numpy, matplotlib, scipy, scikit-learn
2. Compile the code using:
    ```bash
    cd agent
    chmod +x run.sh
    ./run.sh
    ```

3. Run the agent
    ```
    ./main
    ```

** Default parameters for log files are set in the code. 
TODO: Make the code more configurable for different platforms and dependencies.

