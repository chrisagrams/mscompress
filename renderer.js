const worker = new Worker('./worker.js');
console.log("Running system detection...");
worker.onmessage = (e) => { 
   console.log("Number of processors : ", e.data);
   document.querySelector('h1').innerHTML = "get_threads(): " + e.data;
   
   //terminate webworker
   worker.terminate();
   
}
worker.onerror = (event) => {
  console.log(event.message, event);
};