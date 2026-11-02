/* Keep thematic settings compact: one open section, remembered on this phone. */
(()=>{
 let selected='';
 window.pikaApplyConfigAccordion=()=>{
  const content=document.getElementById('content');if(!content)return;selected='';
  content.querySelectorAll(':scope > section.config-section').forEach((section,index)=>{
   const heading=section.querySelector(':scope > .config-section-heading');if(!heading)return;
   const details=document.createElement('details');details.className=section.className;details.id=section.id||'config-section-'+index;details.setAttribute('name','config-groups');
   const summary=document.createElement('summary');summary.className='config-section-heading';const label=document.createElement('div');while(heading.firstChild)label.append(heading.firstChild);summary.append(label);heading.remove();
   const panel=document.createElement('div');panel.className='config-panel';while(section.firstChild)panel.append(section.firstChild);details.append(summary,panel);details.open=selected===details.id;
   details.addEventListener('toggle',()=>{
    if(!details.isConnected)return;
    if(details.open){selected=details.id;content.querySelectorAll('details.config-section[open]').forEach(other=>{if(other!==details)other.open=false})}
    else if(selected===details.id)selected='';

   });
   section.replaceWith(details);
  });
 };
})();
