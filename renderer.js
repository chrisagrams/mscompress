const worker = new Worker('./worker.js');
worker.onmessage = (e) => { 
   
   //print result on console and h1 tag
   console.log("worker : ", e.data);
   document.querySelector('h1').innerHTML = "get_time(): " + e.data;
   //terminate webworker
   worker.terminate();
   
   //set it to undefined
   worker = undefined;
}
worker.onerror = (event) => {
  console.log(event.message, event);
};