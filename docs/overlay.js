document.addEventListener('DOMContentLoaded', () => {
  // Find all links wrapping images that point to a screenshot
  const imageLinks = document.querySelectorAll('a[href$=".png"], a[href$=".jpg"], a[href$=".jpeg"]');

  imageLinks.forEach(link => {
    link.addEventListener('click', (e) => {
      e.preventDefault();
      showOverlay(link.href, link.querySelector('img')?.alt || '');
    });
  });

  function showOverlay(src, altText) {
    let modal = document.getElementById('image-overlay-modal');

    // Create modal elements dynamically if they don't exist
    if (!modal) {
      modal = document.createElement('div');
      modal.id = 'image-overlay-modal';
      modal.className = 'image-overlay-modal';

      const closeBtn = document.createElement('span');
      closeBtn.className = 'image-overlay-close';
      closeBtn.innerHTML = '&times;';
      modal.appendChild(closeBtn);

      const modalImg = document.createElement('img');
      modalImg.className = 'image-overlay-content';
      modalImg.id = 'image-overlay-img';
      modal.appendChild(modalImg);

      const caption = document.createElement('div');
      caption.className = 'image-overlay-caption';
      modal.appendChild(caption);

      document.body.appendChild(modal);

      // Close on clicking the backdrop/close button
      modal.addEventListener('click', (e) => {
        if (e.target !== modalImg) {
          modal.classList.remove('open');
        }
      });

      // Close on ESC key
      document.addEventListener('keydown', (e) => {
        if (e.key === 'Escape') {
          modal.classList.remove('open');
        }
      });
    }

    // Set src and caption
    const modalImg = modal.querySelector('.image-overlay-content');
    const caption = modal.querySelector('.image-overlay-caption');
    modalImg.src = src;
    caption.textContent = altText;

    // Show modal with transition
    requestAnimationFrame(() => {
      modal.classList.add('open');
    });
  }
});
